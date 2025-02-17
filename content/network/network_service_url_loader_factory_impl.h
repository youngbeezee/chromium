// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_NETWORK_NETWORK_SERVICE_URL_LOADER_FACTORY_IMPL_H_
#define CONTENT_NETWORK_NETWORK_SERVICE_URL_LOADER_FACTORY_IMPL_H_

#include "base/macros.h"
#include "content/common/url_loader_factory.mojom.h"

namespace content {

class NetworkContext;

// This class is an implementation of mojom::URLLoaderFactory that creates
// a mojom::URLLoader.
class NetworkServiceURLLoaderFactoryImpl : public mojom::URLLoaderFactory {
 public:
  // NOTE: |context| must outlive this instance.
  explicit NetworkServiceURLLoaderFactoryImpl(NetworkContext* context);

  ~NetworkServiceURLLoaderFactoryImpl() override;

  // mojom::URLLoaderFactory implementation.
  void CreateLoaderAndStart(mojom::URLLoaderAssociatedRequest request,
                            int32_t routing_id,
                            int32_t request_id,
                            uint32_t options,
                            const ResourceRequest& url_request,
                            mojom::URLLoaderClientPtr client) override;
  void SyncLoad(int32_t routing_id,
                int32_t request_id,
                const ResourceRequest& request,
                SyncLoadCallback callback) override;

 private:
  // Not owned.
  NetworkContext* context_;

  DISALLOW_COPY_AND_ASSIGN(NetworkServiceURLLoaderFactoryImpl);
};

}  // namespace content

#endif  // CONTENT_NETWORK_NETWORK_SERVICE_URL_LOADER_FACTORY_IMPL_H_
