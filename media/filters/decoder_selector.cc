// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/filters/decoder_selector.h"

#include <utility>

#include "base/bind.h"
#include "base/callback_helpers.h"
#include "base/logging.h"
#include "base/single_thread_task_runner.h"
#include "build/build_config.h"
#include "media/base/audio_decoder.h"
#include "media/base/bind_to_current_loop.h"
#include "media/base/cdm_context.h"
#include "media/base/demuxer_stream.h"
#include "media/base/media_log.h"
#include "media/base/video_decoder.h"
#include "media/filters/decoder_stream_traits.h"
#include "media/filters/decrypting_demuxer_stream.h"

#if !defined(DISABLE_FFMPEG_VIDEO_DECODERS)
#include "media/filters/decrypting_audio_decoder.h"
#include "media/filters/decrypting_video_decoder.h"
#endif

namespace media {

static bool HasValidStreamConfig(DemuxerStream* stream) {
  switch (stream->type()) {
    case DemuxerStream::AUDIO:
      return stream->audio_decoder_config().IsValidConfig();
    case DemuxerStream::VIDEO:
      return stream->video_decoder_config().IsValidConfig();
    case DemuxerStream::TEXT:
    case DemuxerStream::UNKNOWN:
      NOTREACHED();
  }
  return false;
}

template <DemuxerStream::Type StreamType>
DecoderSelector<StreamType>::DecoderSelector(
    const scoped_refptr<base::SingleThreadTaskRunner>& task_runner,
    ScopedVector<Decoder> decoders,
    MediaLog* media_log)
    : task_runner_(task_runner),
      decoders_(std::move(decoders)),
      media_log_(media_log),
      input_stream_(nullptr),
      weak_ptr_factory_(this) {}

template <DemuxerStream::Type StreamType>
DecoderSelector<StreamType>::~DecoderSelector() {
  DVLOG(2) << __func__;
  DCHECK(task_runner_->BelongsToCurrentThread());

  if (!select_decoder_cb_.is_null())
    ReturnNullDecoder();

  decoder_.reset();
  decrypted_stream_.reset();
}

template <DemuxerStream::Type StreamType>
void DecoderSelector<StreamType>::SelectDecoder(
    StreamTraits* traits,
    DemuxerStream* stream,
    CdmContext* cdm_context,
    const SelectDecoderCB& select_decoder_cb,
    const typename Decoder::OutputCB& output_cb,
    const base::Closure& waiting_for_decryption_key_cb) {
  DVLOG(2) << __func__;
  DCHECK(task_runner_->BelongsToCurrentThread());
  DCHECK(traits);
  DCHECK(stream);
  DCHECK(select_decoder_cb_.is_null());

  cdm_context_ = cdm_context;
  waiting_for_decryption_key_cb_ = waiting_for_decryption_key_cb;

  // Make sure |select_decoder_cb| runs on a different execution stack.
  select_decoder_cb_ = BindToCurrentLoop(select_decoder_cb);

  if (!HasValidStreamConfig(stream)) {
    DLOG(ERROR) << "Invalid stream config.";
    ReturnNullDecoder();
    return;
  }

  traits_ = traits;
  input_stream_ = stream;
  output_cb_ = output_cb;

  // When there is a CDM attached, always try the decrypting decoder or
  // demuxer-stream first.
  if (cdm_context_) {
#if !defined(DISABLE_FFMPEG_VIDEO_DECODERS)
    InitializeDecryptingDecoder();
#else
    InitializeDecryptingDemuxerStream();
#endif
    return;
  }

  config_ = StreamTraits::GetDecoderConfig(input_stream_);

  // If the input stream is encrypted, CdmContext must be non-null.
  DCHECK(!config_.is_encrypted());

  InitializeDecoder();
}

#if !defined(DISABLE_FFMPEG_VIDEO_DECODERS)
template <DemuxerStream::Type StreamType>
void DecoderSelector<StreamType>::InitializeDecryptingDecoder() {
  DVLOG(2) << __func__;
  decoder_.reset(new typename StreamTraits::DecryptingDecoderType(
      task_runner_, media_log_, waiting_for_decryption_key_cb_));

  traits_->InitializeDecoder(
      decoder_.get(), StreamTraits::GetDecoderConfig(input_stream_),
      input_stream_->liveness() == DemuxerStream::LIVENESS_LIVE, cdm_context_,
      base::Bind(&DecoderSelector<StreamType>::DecryptingDecoderInitDone,
                 weak_ptr_factory_.GetWeakPtr()),
      output_cb_);
}

template <DemuxerStream::Type StreamType>
void DecoderSelector<StreamType>::DecryptingDecoderInitDone(bool success) {
  DVLOG(2) << __func__ << ": success=" << success;
  DCHECK(task_runner_->BelongsToCurrentThread());

  if (success) {
    DVLOG(1) << __func__ << ": " << decoder_->GetDisplayName() << " selected.";
    base::ResetAndReturn(&select_decoder_cb_)
        .Run(std::move(decoder_), std::unique_ptr<DecryptingDemuxerStream>());
    return;
  }

  decoder_.reset();

  // When we get here decrypt-and-decode is not supported. Try to use
  // DecryptingDemuxerStream to do decrypt-only.
  InitializeDecryptingDemuxerStream();
}
#endif  // !defined(DISABLE_FFMPEG_VIDEO_DECODERS)

template <DemuxerStream::Type StreamType>
void DecoderSelector<StreamType>::InitializeDecryptingDemuxerStream() {
  decrypted_stream_.reset(new DecryptingDemuxerStream(
      task_runner_, media_log_, waiting_for_decryption_key_cb_));

  decrypted_stream_->Initialize(
      input_stream_, cdm_context_,
      base::Bind(&DecoderSelector<StreamType>::DecryptingDemuxerStreamInitDone,
                 weak_ptr_factory_.GetWeakPtr()));
}

template <DemuxerStream::Type StreamType>
void DecoderSelector<StreamType>::DecryptingDemuxerStreamInitDone(
    PipelineStatus status) {
  DVLOG(2) << __func__
           << ": status=" << MediaLog::PipelineStatusToString(status);
  DCHECK(task_runner_->BelongsToCurrentThread());
  DCHECK(cdm_context_);

  // If DecryptingDemuxerStream initialization succeeded, we'll use it to do
  // decryption and use a decoder to decode the clear stream. Otherwise, we'll
  // try to see whether any decoder can decrypt-and-decode the encrypted stream
  // directly. So in both cases, we'll initialize the decoders.

  if (status == PIPELINE_OK) {
    input_stream_ = decrypted_stream_.get();
    config_ = StreamTraits::GetDecoderConfig(input_stream_);
    DCHECK(!config_.is_encrypted());
  } else {
    decrypted_stream_.reset();
    config_ = StreamTraits::GetDecoderConfig(input_stream_);

    // Prefer decrypting decoder by using an encrypted config.
    if (!config_.is_encrypted())
      config_.SetIsEncrypted(true);
  }

  InitializeDecoder();
}

template <DemuxerStream::Type StreamType>
void DecoderSelector<StreamType>::InitializeDecoder() {
  DVLOG(2) << __func__;
  DCHECK(task_runner_->BelongsToCurrentThread());
  DCHECK(!decoder_);

  if (decoders_.empty()) {
    ReturnNullDecoder();
    return;
  }

  decoder_.reset(decoders_.front());
  decoders_.weak_erase(decoders_.begin());

  traits_->InitializeDecoder(
      decoder_.get(), config_,
      input_stream_->liveness() == DemuxerStream::LIVENESS_LIVE, cdm_context_,
      base::Bind(&DecoderSelector<StreamType>::DecoderInitDone,
                 weak_ptr_factory_.GetWeakPtr()),
      output_cb_);
}

template <DemuxerStream::Type StreamType>
void DecoderSelector<StreamType>::DecoderInitDone(bool success) {
  DVLOG(2) << __func__ << ": success=" << success;
  DCHECK(task_runner_->BelongsToCurrentThread());

  if (!success) {
    decoder_.reset();
    InitializeDecoder();
    return;
  }

  DVLOG(1) << __func__ << ": " << decoder_->GetDisplayName()
           << " selected. DecryptingDemuxerStream "
           << (decrypted_stream_ ? "also" : "not") << " selected.";

  base::ResetAndReturn(&select_decoder_cb_)
      .Run(std::move(decoder_), std::move(decrypted_stream_));
}

template <DemuxerStream::Type StreamType>
void DecoderSelector<StreamType>::ReturnNullDecoder() {
  DVLOG(1) << __func__ << ": No decoder selected.";
  DCHECK(task_runner_->BelongsToCurrentThread());
  base::ResetAndReturn(&select_decoder_cb_)
      .Run(std::unique_ptr<Decoder>(),
           std::unique_ptr<DecryptingDemuxerStream>());
}

// These forward declarations tell the compiler that we will use
// DecoderSelector with these arguments, allowing us to keep these definitions
// in our .cc without causing linker errors. This also means if anyone tries to
// instantiate a DecoderSelector with anything but these two specializations
// they'll most likely get linker errors.
template class DecoderSelector<DemuxerStream::AUDIO>;
template class DecoderSelector<DemuxerStream::VIDEO>;

}  // namespace media
