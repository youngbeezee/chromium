// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_MEDIA_ROUTER_MEDIA_ROUTER_WEBUI_MESSAGE_HANDLER_H_
#define CHROME_BROWSER_UI_WEBUI_MEDIA_ROUTER_MEDIA_ROUTER_WEBUI_MESSAGE_HANDLER_H_

#include <unordered_map>
#include <vector>

#include "base/macros.h"
#include "chrome/browser/ui/webui/media_router/media_cast_mode.h"
#include "chrome/browser/ui/webui/media_router/media_sink_with_cast_modes.h"
#include "chrome/common/media_router/issue.h"
#include "components/signin/core/browser/account_info.h"
#include "content/public/browser/web_ui_message_handler.h"
#include "ui/gfx/geometry/size.h"

namespace base {
class DictionaryValue;
class ListValue;
}  // namespace base

namespace content {
class WebUI;
}

namespace media_router {

class Issue;
class MediaRoute;
class MediaRouterUI;

// The handler for Javascript messages related to the media router dialog.
class MediaRouterWebUIMessageHandler : public content::WebUIMessageHandler {
 public:
  explicit MediaRouterWebUIMessageHandler(MediaRouterUI* media_router_ui);
  ~MediaRouterWebUIMessageHandler() override;

  // Methods to update the status displayed by the dialog.
  void UpdateSinks(const std::vector<MediaSinkWithCastModes>& sinks);
  void UpdateRoutes(const std::vector<MediaRoute>& routes,
                    const std::vector<MediaRoute::Id>& joinable_route_ids,
                    const std::unordered_map<MediaRoute::Id, MediaCastMode>&
                        current_cast_modes);
  void UpdateCastModes(const CastModeSet& cast_modes,
                       const std::string& source_host);
  void OnCreateRouteResponseReceived(const MediaSink::Id& sink_id,
                                     const MediaRoute* route);
  void ReturnSearchResult(const std::string& sink_id);

  void UpdateIssue(const Issue& issue);
  void ClearIssue();

  // Updates the maximum dialog height to allow the WebUI properly scale when
  // the browser window changes.
  void UpdateMaxDialogHeight(int height);

  void SetWebUIForTest(content::WebUI* webui);
  void set_incognito_for_test(bool incognito) { incognito_ = incognito; }

 private:
  FRIEND_TEST_ALL_PREFIXES(MediaRouterWebUIMessageHandlerTest,
                           RecordCastModeSelection);
  FRIEND_TEST_ALL_PREFIXES(MediaRouterWebUIMessageHandlerTest,
                           RetrieveCastModeSelection);

  // WebUIMessageHandler implementation.
  void RegisterMessages() override;

  // Handlers for JavaScript messages.
  // See media_router_ui_interface.js for documentation on parameters.
  void OnRequestInitialData(const base::ListValue* args);
  void OnCreateRoute(const base::ListValue* args);
  void OnAcknowledgeFirstRunFlow(const base::ListValue* args);
  void OnActOnIssue(const base::ListValue* args);
  void OnCloseRoute(const base::ListValue* args);
  void OnJoinRoute(const base::ListValue* args);
  void OnCloseDialog(const base::ListValue* args);
  void OnReportBlur(const base::ListValue* args);
  void OnReportClickedSinkIndex(const base::ListValue* args);
  void OnReportFilter(const base::ListValue* args);
  void OnReportInitialAction(const base::ListValue* args);
  void OnReportInitialState(const base::ListValue* args);
  void OnReportNavigateToView(const base::ListValue* args);
  void OnReportRouteCreation(const base::ListValue* args);
  void OnReportRouteCreationOutcome(const base::ListValue* args);
  void OnReportSelectedCastMode(const base::ListValue* args);
  void OnReportSinkCount(const base::ListValue* args);
  void OnReportTimeToClickSink(const base::ListValue* args);
  void OnReportTimeToInitialActionClose(const base::ListValue* args);
  void OnSearchSinksAndCreateRoute(const base::ListValue* args);
  void OnInitialDataReceived(const base::ListValue* args);

  // Performs an action for an Issue of |type|.
  // |args| contains additional parameter that varies based on |type|.
  // Returns |true| if the action was successfully performed.
  bool ActOnIssueType(IssueInfo::Action type,
                      const base::DictionaryValue* args);

  // May update the first run flow related properties in the WebUI. This is
  // called after the initial data is received to avoid unnecessary work when
  // initializing the WebUI.
  void MaybeUpdateFirstRunFlowData();

  // Returns the current cast mode for the route with ID |route_id| or -1 if the
  // route has no current cast mode.
  int CurrentCastModeForRouteId(
      const MediaRoute::Id& route_id,
      const std::unordered_map<MediaRoute::Id, MediaCastMode>&
          current_cast_modes) const;

  // Converts |routes| and |joinable_route_ids| into base::ListValue that can be
  // passed to WebUI.
  std::unique_ptr<base::ListValue> RoutesToValue(
      const std::vector<MediaRoute>& routes,
      const std::vector<MediaRoute::Id>& joinable_route_ids,
      const std::unordered_map<MediaRoute::Id, MediaCastMode>&
          current_cast_modes) const;

  // Retrieve the account info for email and domain of signed in users. This is
  // used when updating sinks to determine if identity should be displayed.
  // Marked virtual for tests.
  virtual AccountInfo GetAccountInfo();

  // |true| if the associated Profile is incognito.
  bool incognito_;

  // Keeps track of whether a command to close the dialog has been issued.
  bool dialog_closing_;

  MediaRouterUI* media_router_ui_;

  DISALLOW_COPY_AND_ASSIGN(MediaRouterWebUIMessageHandler);
};

}  // namespace media_router

#endif  // CHROME_BROWSER_UI_WEBUI_MEDIA_ROUTER_MEDIA_ROUTER_WEBUI_MESSAGE_HANDLER_H_
