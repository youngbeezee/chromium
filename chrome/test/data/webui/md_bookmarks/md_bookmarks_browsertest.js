// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview Test suite for the Material Design bookmarks page.
 */
var ROOT_PATH = '../../../../../';

GEN_INCLUDE(
    [ROOT_PATH + 'chrome/test/data/webui/polymer_browser_test_base.js']);
GEN('#include "base/command_line.h"');

function MaterialBookmarksBrowserTest() {}

MaterialBookmarksBrowserTest.prototype = {
  __proto__: PolymerTest.prototype,

  browsePreload: 'chrome://bookmarks',

  commandLineSwitches: [{switchName: 'enable-features',
                         switchValue: 'MaterialDesignBookmarks'}],

  extraLibraries: PolymerTest.getLibraries(ROOT_PATH).concat([
    'test_store.js',
    'test_util.js',
  ]),
};

function MaterialBookmarksActionsTest() {}

MaterialBookmarksActionsTest.prototype = {
  __proto__: MaterialBookmarksBrowserTest.prototype,

  extraLibraries: MaterialBookmarksBrowserTest.prototype.extraLibraries.concat([
    'actions_test.js',
  ]),
};

TEST_F('MaterialBookmarksActionsTest', 'All', function() {
  mocha.run();
});

function MaterialBookmarksAppTest() {}

MaterialBookmarksAppTest.prototype = {
  __proto__: MaterialBookmarksBrowserTest.prototype,

  extraLibraries: MaterialBookmarksBrowserTest.prototype.extraLibraries.concat([
    'app_test.js',
  ]),
};

TEST_F('MaterialBookmarksAppTest', 'All', function() {
  mocha.run();
});

function MaterialBookmarksDNDManagerTest() {}

MaterialBookmarksDNDManagerTest.prototype = {
  __proto__: MaterialBookmarksBrowserTest.prototype,

  extraLibraries: MaterialBookmarksBrowserTest.prototype.extraLibraries.concat([
    'dnd_manager_test.js',
  ]),
};

TEST_F('MaterialBookmarksDNDManagerTest', 'All', function() {
  mocha.run();
});

function MaterialBookmarksEditDialogTest() {}

MaterialBookmarksEditDialogTest.prototype = {
  __proto__: MaterialBookmarksBrowserTest.prototype,

  extraLibraries: MaterialBookmarksBrowserTest.prototype.extraLibraries.concat([
    'edit_dialog_test.js',
  ]),
};

TEST_F('MaterialBookmarksEditDialogTest', 'All', function() {
  mocha.run();
});

function MaterialBookmarksItemTest() {}

MaterialBookmarksItemTest.prototype = {
  __proto__: MaterialBookmarksBrowserTest.prototype,

  extraLibraries: MaterialBookmarksBrowserTest.prototype.extraLibraries.concat([
    'item_test.js',
  ]),
};

TEST_F('MaterialBookmarksItemTest', 'All', function() {
  mocha.run();
});

function MaterialBookmarksListTest() {}

MaterialBookmarksListTest.prototype = {
  __proto__: MaterialBookmarksBrowserTest.prototype,

  extraLibraries: MaterialBookmarksBrowserTest.prototype.extraLibraries.concat([
    'list_test.js',
  ]),
};

TEST_F('MaterialBookmarksListTest', 'All', function() {
  mocha.run();
});

function MaterialBookmarksReducersTest() {}

MaterialBookmarksReducersTest.prototype = {
  __proto__: MaterialBookmarksBrowserTest.prototype,

  extraLibraries: MaterialBookmarksBrowserTest.prototype.extraLibraries.concat([
    'reducers_test.js',
  ]),
};

TEST_F('MaterialBookmarksReducersTest', 'All', function() {
  mocha.run();
});

function MaterialBookmarksRouterTest() {}

MaterialBookmarksRouterTest.prototype = {
  __proto__: MaterialBookmarksBrowserTest.prototype,

  extraLibraries: MaterialBookmarksBrowserTest.prototype.extraLibraries.concat([
    'router_test.js',
  ]),
};

TEST_F('MaterialBookmarksRouterTest', 'All', function() {
  mocha.run();
});

function MaterialBookmarksFolderNodeTest() {}

MaterialBookmarksFolderNodeTest.prototype = {
  __proto__: MaterialBookmarksBrowserTest.prototype,

  extraLibraries: MaterialBookmarksBrowserTest.prototype.extraLibraries.concat([
    'folder_node_test.js',
  ]),
};

TEST_F('MaterialBookmarksFolderNodeTest', 'All', function() {
  mocha.run();
});

function MaterialBookmarksStoreClientTest() {}

MaterialBookmarksStoreClientTest.prototype = {
  __proto__: MaterialBookmarksBrowserTest.prototype,

  extraLibraries: MaterialBookmarksBrowserTest.prototype.extraLibraries.concat([
    'store_client_test.js',
  ]),
};

TEST_F('MaterialBookmarksStoreClientTest', 'All', function() {
  mocha.run();
});

function MaterialBookmarksToolbarTest() {}

MaterialBookmarksToolbarTest.prototype = {
  __proto__: MaterialBookmarksBrowserTest.prototype,

  extraLibraries: MaterialBookmarksBrowserTest.prototype.extraLibraries.concat([
    'toolbar_test.js',
  ]),
};

TEST_F('MaterialBookmarksToolbarTest', 'All', function() {
  mocha.run();
});

function MaterialBookmarksUtilTest() {}

MaterialBookmarksUtilTest.prototype = {
  __proto__: MaterialBookmarksBrowserTest.prototype,

  extraLibraries: MaterialBookmarksBrowserTest.prototype.extraLibraries.concat([
    'util_test.js',
  ]),
};

TEST_F('MaterialBookmarksUtilTest', 'All', function() {
  mocha.run();
});
