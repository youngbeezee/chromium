// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REMOTING_CLIENT_IOS_SESSION_REMOTING_CLIENT_SESSION_DELEGATE_H_
#define REMOTING_CLIENT_IOS_SESSION_REMOTING_CLIENT_SESSION_DELEGATE_H_

#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "remoting/client/chromoting_session.h"
#include "remoting/protocol/connection_to_host.h"

@class RemotingClient;

namespace remoting {

class ChromotingClientRuntime;

class RemotingClientSessonDelegate : public ChromotingSession::Delegate {
 public:
  RemotingClientSessonDelegate(RemotingClient* client);
  ~RemotingClientSessonDelegate() override;

  // ChromotingSession::Delegate implementation

  void OnConnectionState(protocol::ConnectionToHost::State state,
                         protocol::ErrorCode error) override;
  void CommitPairingCredentials(const std::string& host,
                                const std::string& id,
                                const std::string& secret) override;
  void FetchThirdPartyToken(const std::string& token_url,
                            const std::string& client_id,
                            const std::string& scope) override;
  void SetCapabilities(const std::string& capabilities) override;
  void HandleExtensionMessage(const std::string& type,
                              const std::string& message) override;

  base::WeakPtr<RemotingClientSessonDelegate> GetWeakPtr();

 private:
  ChromotingClientRuntime* runtime_;
  id client_;

  base::WeakPtrFactory<RemotingClientSessonDelegate> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(RemotingClientSessonDelegate);
};

}  // namespace remoting

#endif  // REMOTING_CLIENT_IOS_SESSION_REMOTING_CLIENT_SESSION_DELEGATE_H_
