// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/location.h"
#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/memory/ref_counted.h"
#include "base/message_loop/message_loop.h"
#include "base/path_service.h"
#include "base/run_loop.h"
#include "base/test/sequenced_worker_pool_owner.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/values.h"
#include "base/version.h"
#include "components/prefs/testing_pref_service.h"
#include "components/update_client/crx_update_item.h"
#include "components/update_client/persisted_data.h"
#include "components/update_client/ping_manager.h"
#include "components/update_client/test_configurator.h"
#include "components/update_client/test_installer.h"
#include "components/update_client/update_checker.h"
#include "components/update_client/update_client_errors.h"
#include "components/update_client/update_client_internal.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace update_client {

namespace {

using base::FilePath;

// Makes a copy of the file specified by |from_path| in a temporary directory
// and returns the path of the copy. Returns true if successful. Cleans up if
// there was an error creating the copy.
bool MakeTestFile(const FilePath& from_path, FilePath* to_path) {
  FilePath temp_dir;
  bool result =
      CreateNewTempDirectory(FILE_PATH_LITERAL("update_client"), &temp_dir);
  if (!result)
    return false;

  FilePath temp_file;
  result = CreateTemporaryFileInDir(temp_dir, &temp_file);
  if (!result)
    return false;

  result = CopyFile(from_path, temp_file);
  if (!result) {
    DeleteFile(temp_file, false);
    return false;
  }

  *to_path = temp_file;
  return true;
}

using Events = UpdateClient::Observer::Events;

class MockObserver : public UpdateClient::Observer {
 public:
  MOCK_METHOD2(OnEvent, void(Events event, const std::string&));
};

}  // namespace

using ::testing::_;
using ::testing::AtLeast;
using ::testing::AnyNumber;
using ::testing::DoAll;
using ::testing::InSequence;
using ::testing::Invoke;
using ::testing::Mock;
using ::testing::Return;

using std::string;

class FakePingManagerImpl : public PingManager {
 public:
  struct PingData {
    std::string id;
    base::Version previous_version;
    base::Version next_version;
    int error_category = 0;
    int error_code = 0;
    int extra_code1 = 0;
    int diff_error_category = 0;
    int diff_error_code = 0;
    bool diff_update_failed = false;
  };

  explicit FakePingManagerImpl(const scoped_refptr<Configurator>& config);
  ~FakePingManagerImpl() override;

  bool SendPing(const Component& component) override;

  const std::vector<PingData>& ping_data() const;

 private:
  std::vector<PingData> ping_data_;
  DISALLOW_COPY_AND_ASSIGN(FakePingManagerImpl);
};

FakePingManagerImpl::FakePingManagerImpl(
    const scoped_refptr<Configurator>& config)
    : PingManager(config) {}

FakePingManagerImpl::~FakePingManagerImpl() {
}

bool FakePingManagerImpl::SendPing(const Component& component) {
  PingData ping_data;
  ping_data.id = component.id_;
  ping_data.previous_version = component.previous_version_;
  ping_data.next_version = component.next_version_;
  ping_data.error_category = component.error_category_;
  ping_data.error_code = component.error_code_;
  ping_data.extra_code1 = component.extra_code1_;
  ping_data.diff_error_category = component.diff_error_category_;
  ping_data.diff_error_code = component.diff_error_code_;
  ping_data.diff_update_failed = component.diff_update_failed();
  ping_data_.push_back(ping_data);
  return true;
}

const std::vector<FakePingManagerImpl::PingData>&
FakePingManagerImpl::ping_data() const {
  return ping_data_;
}

class UpdateClientTest : public testing::Test {
 public:
  UpdateClientTest();
  ~UpdateClientTest() override;

 protected:
  void RunThreads();

  // Returns the full path to a test file.
  static base::FilePath TestFilePath(const char* file);

  scoped_refptr<update_client::TestConfigurator> config() { return config_; }
  update_client::PersistedData* metadata() { return metadata_.get(); }

  base::Closure quit_closure() { return quit_closure_; }

 private:
  static constexpr int kNumWorkerThreads_ = 2;

  base::MessageLoopForUI message_loop_;
  base::RunLoop runloop_;
  base::Closure quit_closure_;

  std::unique_ptr<base::SequencedWorkerPoolOwner> worker_pool_;

  scoped_refptr<update_client::TestConfigurator> config_;
  std::unique_ptr<TestingPrefServiceSimple> pref_;
  std::unique_ptr<update_client::PersistedData> metadata_;

  DISALLOW_COPY_AND_ASSIGN(UpdateClientTest);
};

constexpr int UpdateClientTest::kNumWorkerThreads_;

UpdateClientTest::UpdateClientTest()
    : worker_pool_(
          base::MakeUnique<base::SequencedWorkerPoolOwner>(kNumWorkerThreads_,
                                                           "test")),
      pref_(base::MakeUnique<TestingPrefServiceSimple>()) {
  quit_closure_ = runloop_.QuitClosure();

  auto pool = worker_pool_->pool();
  config_ = base::MakeShared<TestConfigurator>(
      pool->GetSequencedTaskRunner(pool->GetSequenceToken()),
      message_loop_.task_runner());
  PersistedData::RegisterPrefs(pref_->registry());
  metadata_ = base::MakeUnique<PersistedData>(pref_.get());
}

UpdateClientTest::~UpdateClientTest() {
}

void UpdateClientTest::RunThreads() {
  runloop_.Run();
}

base::FilePath UpdateClientTest::TestFilePath(const char* file) {
  base::FilePath path;
  PathService::Get(base::DIR_SOURCE_ROOT, &path);
  return path.AppendASCII("components")
      .AppendASCII("test")
      .AppendASCII("data")
      .AppendASCII("update_client")
      .AppendASCII(file);
}

// Tests the scenario where one update check is done for one CRX. The CRX
// has no update.
TEST_F(UpdateClientTest, OneCrxNoUpdate) {
  class DataCallbackFake {
   public:
    static void Callback(const std::vector<std::string>& ids,
                         std::vector<CrxComponent>* components) {
      CrxComponent crx;
      crx.name = "test_jebg";
      crx.pk_hash.assign(jebg_hash, jebg_hash + arraysize(jebg_hash));
      crx.version = base::Version("0.9");
      crx.installer = base::MakeShared<TestInstaller>();
      components->push_back(crx);
    }
  };

  class CompletionCallbackFake {
   public:
    static void Callback(const base::Closure& quit_closure, Error error) {
      EXPECT_EQ(Error::NONE, error);
      quit_closure.Run();
    }
  };

  class FakeUpdateChecker : public UpdateChecker {
   public:
    static std::unique_ptr<UpdateChecker> Create(
        const scoped_refptr<Configurator>& config,
        PersistedData* metadata) {
      return base::MakeUnique<FakeUpdateChecker>();
    }

    bool CheckForUpdates(
        const std::vector<std::string>& ids_to_check,
        const IdToComponentPtrMap& components,
        const std::string& additional_attributes,
        bool enabled_component_updates,
        const UpdateCheckCallback& update_check_callback) override {
      EXPECT_TRUE(enabled_component_updates);
      EXPECT_EQ(1u, ids_to_check.size());
      const std::string id = "jebgalgnebhfojomionfpkfelancnnkf";
      EXPECT_EQ(id, ids_to_check.front());
      EXPECT_EQ(1u, components.count(id));

      auto& component = components.at(id);

      EXPECT_FALSE(component->on_demand());

      UpdateResponse::Result result;
      result.extension_id = id;
      result.status = "noupdate";
      component->SetParseResult(result);

      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE, base::Bind(update_check_callback, 0, 0));

      return true;
    }
  };

  class FakeCrxDownloader : public CrxDownloader {
   public:
    static std::unique_ptr<CrxDownloader> Create(
        bool is_background_download,
        net::URLRequestContextGetter* context_getter,
        const scoped_refptr<base::SequencedTaskRunner>& task_runner) {
      return base::MakeUnique<FakeCrxDownloader>();
    }

    FakeCrxDownloader()
        : CrxDownloader(base::ThreadTaskRunnerHandle::Get(), nullptr) {}

   private:
    void DoStartDownload(const GURL& url) override { EXPECT_TRUE(false); }
  };

  class FakePingManager : public FakePingManagerImpl {
   public:
    explicit FakePingManager(const scoped_refptr<Configurator>& config)
        : FakePingManagerImpl(config) {}
    ~FakePingManager() override { EXPECT_TRUE(ping_data().empty()); }
  };

  scoped_refptr<UpdateClient> update_client =
      base::MakeShared<UpdateClientImpl>(
          config(), base::MakeUnique<FakePingManager>(config()),
          &FakeUpdateChecker::Create, &FakeCrxDownloader::Create);

  MockObserver observer;
  InSequence seq;
  EXPECT_CALL(observer, OnEvent(Events::COMPONENT_CHECKING_FOR_UPDATES,
                                "jebgalgnebhfojomionfpkfelancnnkf")).Times(1);
  EXPECT_CALL(observer, OnEvent(Events::COMPONENT_NOT_UPDATED,
                                "jebgalgnebhfojomionfpkfelancnnkf")).Times(1);

  update_client->AddObserver(&observer);

  const std::vector<std::string> ids = {"jebgalgnebhfojomionfpkfelancnnkf"};
  update_client->Update(
      ids, base::Bind(&DataCallbackFake::Callback),
      base::Bind(&CompletionCallbackFake::Callback, quit_closure()));

  RunThreads();

  update_client->RemoveObserver(&observer);
}

// Tests the scenario where two CRXs are checked for updates. On CRX has
// an update, the other CRX does not.
TEST_F(UpdateClientTest, TwoCrxUpdateNoUpdate) {
  class DataCallbackFake {
   public:
    static void Callback(const std::vector<std::string>& ids,
                         std::vector<CrxComponent>* components) {
      CrxComponent crx1;
      crx1.name = "test_jebg";
      crx1.pk_hash.assign(jebg_hash, jebg_hash + arraysize(jebg_hash));
      crx1.version = base::Version("0.9");
      crx1.installer = base::MakeShared<TestInstaller>();

      CrxComponent crx2;
      crx2.name = "test_abag";
      crx2.pk_hash.assign(abag_hash, abag_hash + arraysize(abag_hash));
      crx2.version = base::Version("2.2");
      crx2.installer = base::MakeShared<TestInstaller>();

      components->push_back(crx1);
      components->push_back(crx2);
    }
  };

  class CompletionCallbackFake {
   public:
    static void Callback(const base::Closure& quit_closure, Error error) {
      EXPECT_EQ(Error::NONE, error);
      quit_closure.Run();
    }
  };

  class FakeUpdateChecker : public UpdateChecker {
   public:
    static std::unique_ptr<UpdateChecker> Create(
        const scoped_refptr<Configurator>& config,
        PersistedData* metadata) {
      return base::MakeUnique<FakeUpdateChecker>();
    }

    bool CheckForUpdates(
        const std::vector<std::string>& ids_to_check,
        const IdToComponentPtrMap& components,
        const std::string& additional_attributes,
        bool enabled_component_updates,
        const UpdateCheckCallback& update_check_callback) override {
      /*
      Fake the following response:

      <?xml version='1.0' encoding='UTF-8'?>
      <response protocol='3.0'>
        <app appid='jebgalgnebhfojomionfpkfelancnnkf'>
          <updatecheck status='ok'>
            <urls>
              <url codebase='http://localhost/download/'/>
            </urls>
            <manifest version='1.0' prodversionmin='11.0.1.0'>
              <packages>
                <package name='jebgalgnebhfojomionfpkfelancnnkf.crx'
                         hash_sha256='6fc4b93fd11134de1300c2c0bb88c12b644a4ec0fd
                                      7c9b12cb7cc067667bde87'/>
              </packages>
            </manifest>
          </updatecheck>
        </app>
      </response>
      */

      EXPECT_TRUE(enabled_component_updates);
      EXPECT_EQ(2u, ids_to_check.size());

      {
        const std::string id = "jebgalgnebhfojomionfpkfelancnnkf";
        EXPECT_EQ(id, ids_to_check[0]);
        EXPECT_EQ(1u, components.count(id));

        UpdateResponse::Result::Manifest::Package package;
        package.name = "jebgalgnebhfojomionfpkfelancnnkf.crx";
        package.hash_sha256 =
            "6fc4b93fd11134de1300c2c0bb88c12b644a4ec0fd7c9b12cb7cc067667bde87";

        UpdateResponse::Result result;
        result.extension_id = "jebgalgnebhfojomionfpkfelancnnkf";
        result.status = "ok";
        result.crx_urls.push_back(GURL("http://localhost/download/"));
        result.manifest.version = "1.0";
        result.manifest.browser_min_version = "11.0.1.0";
        result.manifest.packages.push_back(package);

        auto& component = components.at(id);
        component->SetParseResult(result);

        EXPECT_FALSE(component->on_demand());
      }

      {
        const std::string id = "abagagagagagagagagagagagagagagag";
        EXPECT_EQ(id, ids_to_check[1]);
        EXPECT_EQ(1u, components.count(id));

        UpdateResponse::Result result;
        result.extension_id = id;
        result.status = "noupdate";

        auto& component = components.at(id);
        component->SetParseResult(result);

        EXPECT_FALSE(component->on_demand());
      }

      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE, base::Bind(update_check_callback, 0, 0));
      return true;
    }
  };

  class FakeCrxDownloader : public CrxDownloader {
   public:
    static std::unique_ptr<CrxDownloader> Create(
        bool is_background_download,
        net::URLRequestContextGetter* context_getter,
        const scoped_refptr<base::SequencedTaskRunner>& task_runner) {
      return base::MakeUnique<FakeCrxDownloader>();
    }

    FakeCrxDownloader()
        : CrxDownloader(base::ThreadTaskRunnerHandle::Get(), nullptr) {}

   private:
    void DoStartDownload(const GURL& url) override {
      DownloadMetrics download_metrics;
      download_metrics.url = url;
      download_metrics.downloader = DownloadMetrics::kNone;
      download_metrics.error = 0;
      download_metrics.downloaded_bytes = 1843;
      download_metrics.total_bytes = 1843;
      download_metrics.download_time_ms = 1000;

      FilePath path;
      EXPECT_TRUE(MakeTestFile(
          TestFilePath("jebgalgnebhfojomionfpkfelancnnkf.crx"), &path));

      Result result;
      result.error = 0;
      result.response = path;
      result.downloaded_bytes = 1843;
      result.total_bytes = 1843;

      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE, base::Bind(&FakeCrxDownloader::OnDownloadProgress,
                                base::Unretained(this), result));

      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE,
          base::Bind(&FakeCrxDownloader::OnDownloadComplete,
                     base::Unretained(this), true, result, download_metrics));
    }
  };

  class FakePingManager : public FakePingManagerImpl {
   public:
    explicit FakePingManager(const scoped_refptr<Configurator>& config)
        : FakePingManagerImpl(config) {}
    ~FakePingManager() override {
      const auto ping_data = FakePingManagerImpl::ping_data();
      EXPECT_EQ(1u, ping_data.size());
      EXPECT_EQ("jebgalgnebhfojomionfpkfelancnnkf", ping_data[0].id);
      EXPECT_EQ(base::Version("0.9"), ping_data[0].previous_version);
      EXPECT_EQ(base::Version("1.0"), ping_data[0].next_version);
      EXPECT_EQ(0, ping_data[0].error_category);
      EXPECT_EQ(0, ping_data[0].error_code);
    }
  };

  scoped_refptr<UpdateClient> update_client =
      base::MakeShared<UpdateClientImpl>(
          config(), base::MakeUnique<FakePingManager>(config()),
          &FakeUpdateChecker::Create, &FakeCrxDownloader::Create);

  MockObserver observer;
  {
    InSequence seq;
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_CHECKING_FOR_UPDATES,
                                  "jebgalgnebhfojomionfpkfelancnnkf")).Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_FOUND,
                                  "jebgalgnebhfojomionfpkfelancnnkf")).Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_DOWNLOADING,
                                  "jebgalgnebhfojomionfpkfelancnnkf"))
        .Times(AtLeast(1));
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_READY,
                                  "jebgalgnebhfojomionfpkfelancnnkf")).Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATED,
                                  "jebgalgnebhfojomionfpkfelancnnkf")).Times(1);
  }
  {
    InSequence seq;
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_CHECKING_FOR_UPDATES,
                                  "abagagagagagagagagagagagagagagag")).Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_NOT_UPDATED,
                                  "abagagagagagagagagagagagagagagag")).Times(1);
  }

  update_client->AddObserver(&observer);

  const std::vector<std::string> ids = {"jebgalgnebhfojomionfpkfelancnnkf",
                                        "abagagagagagagagagagagagagagagag"};
  update_client->Update(
      ids, base::Bind(&DataCallbackFake::Callback),
      base::Bind(&CompletionCallbackFake::Callback, quit_closure()));

  RunThreads();

  update_client->RemoveObserver(&observer);
}

// Tests the update check for two CRXs scenario. Both CRXs have updates.
TEST_F(UpdateClientTest, TwoCrxUpdate) {
  class DataCallbackFake {
   public:
    static void Callback(const std::vector<std::string>& ids,
                         std::vector<CrxComponent>* components) {
      CrxComponent crx1;
      crx1.name = "test_jebg";
      crx1.pk_hash.assign(jebg_hash, jebg_hash + arraysize(jebg_hash));
      crx1.version = base::Version("0.9");
      crx1.installer = base::MakeShared<TestInstaller>();

      CrxComponent crx2;
      crx2.name = "test_ihfo";
      crx2.pk_hash.assign(ihfo_hash, ihfo_hash + arraysize(ihfo_hash));
      crx2.version = base::Version("0.8");
      crx2.installer = base::MakeShared<TestInstaller>();

      components->push_back(crx1);
      components->push_back(crx2);
    }
  };

  class CompletionCallbackFake {
   public:
    static void Callback(const base::Closure& quit_closure, Error error) {
      EXPECT_EQ(Error::NONE, error);
      quit_closure.Run();
    }
  };

  class FakeUpdateChecker : public UpdateChecker {
   public:
    static std::unique_ptr<UpdateChecker> Create(
        const scoped_refptr<Configurator>& config,
        PersistedData* metadata) {
      return base::MakeUnique<FakeUpdateChecker>();
    }

    bool CheckForUpdates(
        const std::vector<std::string>& ids_to_check,
        const IdToComponentPtrMap& components,
        const std::string& additional_attributes,
        bool enabled_component_updates,
        const UpdateCheckCallback& update_check_callback) override {
      /*
      Fake the following response:

      <?xml version='1.0' encoding='UTF-8'?>
      <response protocol='3.0'>
        <app appid='jebgalgnebhfojomionfpkfelancnnkf'>
          <updatecheck status='ok'>
            <urls>
              <url codebase='http://localhost/download/'/>
            </urls>
            <manifest version='1.0' prodversionmin='11.0.1.0'>
              <packages>
                <package name='jebgalgnebhfojomionfpkfelancnnkf.crx'
                         hash_sha256='6fc4b93fd11134de1300c2c0bb88c12b644a4ec0fd
                                      7c9b12cb7cc067667bde87'/>
              </packages>
            </manifest>
          </updatecheck>
        </app>
        <app appid='ihfokbkgjpifnbbojhneepfflplebdkc'>
          <updatecheck status='ok'>
            <urls>
              <url codebase='http://localhost/download/'/>
            </urls>
            <manifest version='1.0' prodversionmin='11.0.1.0'>
              <packages>
                <package name='ihfokbkgjpifnbbojhneepfflplebdkc_1.crx'
                         hash_sha256='813c59747e139a608b3b5fc49633affc6db574373f
                                      309f156ea6d27229c0b3f9'/>
              </packages>
            </manifest>
          </updatecheck>
        </app>
      </response>
      */
      EXPECT_TRUE(enabled_component_updates);
      EXPECT_EQ(2u, ids_to_check.size());

      {
        const std::string id = "jebgalgnebhfojomionfpkfelancnnkf";
        EXPECT_EQ(id, ids_to_check[0]);
        EXPECT_EQ(1u, components.count(id));

        UpdateResponse::Result::Manifest::Package package;
        package.name = "jebgalgnebhfojomionfpkfelancnnkf.crx";
        package.hash_sha256 =
            "6fc4b93fd11134de1300c2c0bb88c12b644a4ec0fd7c9b12cb7cc067667bde87";

        UpdateResponse::Result result;
        result.extension_id = id;
        result.status = "ok";
        result.crx_urls.push_back(GURL("http://localhost/download/"));
        result.manifest.version = "1.0";
        result.manifest.browser_min_version = "11.0.1.0";
        result.manifest.packages.push_back(package);

        auto& component = components.at(id);
        component->SetParseResult(result);

        EXPECT_FALSE(component->on_demand());
      }

      {
        const std::string id = "ihfokbkgjpifnbbojhneepfflplebdkc";
        EXPECT_EQ(id, ids_to_check[1]);
        EXPECT_EQ(1u, components.count(id));

        UpdateResponse::Result::Manifest::Package package;
        package.name = "ihfokbkgjpifnbbojhneepfflplebdkc_1.crx";
        package.hash_sha256 =
            "813c59747e139a608b3b5fc49633affc6db574373f309f156ea6d27229c0b3f9";

        UpdateResponse::Result result;
        result.extension_id = id;
        result.status = "ok";
        result.crx_urls.push_back(GURL("http://localhost/download/"));
        result.manifest.version = "1.0";
        result.manifest.browser_min_version = "11.0.1.0";
        result.manifest.packages.push_back(package);

        auto& component = components.at(id);
        component->SetParseResult(result);

        EXPECT_FALSE(component->on_demand());
      }

      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE, base::Bind(update_check_callback, 0, 0));
      return true;
    }
  };

  class FakeCrxDownloader : public CrxDownloader {
   public:
    static std::unique_ptr<CrxDownloader> Create(
        bool is_background_download,
        net::URLRequestContextGetter* context_getter,
        const scoped_refptr<base::SequencedTaskRunner>& task_runner) {
      return base::MakeUnique<FakeCrxDownloader>();
    }

    FakeCrxDownloader()
        : CrxDownloader(base::ThreadTaskRunnerHandle::Get(), nullptr) {}

   private:
    void DoStartDownload(const GURL& url) override {
      DownloadMetrics download_metrics;
      FilePath path;
      Result result;
      if (url.path() == "/download/jebgalgnebhfojomionfpkfelancnnkf.crx") {
        download_metrics.url = url;
        download_metrics.downloader = DownloadMetrics::kNone;
        download_metrics.error = 0;
        download_metrics.downloaded_bytes = 1843;
        download_metrics.total_bytes = 1843;
        download_metrics.download_time_ms = 1000;

        EXPECT_TRUE(MakeTestFile(
            TestFilePath("jebgalgnebhfojomionfpkfelancnnkf.crx"), &path));

        result.error = 0;
        result.response = path;
        result.downloaded_bytes = 1843;
        result.total_bytes = 1843;
      } else if (url.path() ==
                 "/download/ihfokbkgjpifnbbojhneepfflplebdkc_1.crx") {
        download_metrics.url = url;
        download_metrics.downloader = DownloadMetrics::kNone;
        download_metrics.error = 0;
        download_metrics.downloaded_bytes = 53638;
        download_metrics.total_bytes = 53638;
        download_metrics.download_time_ms = 2000;

        EXPECT_TRUE(MakeTestFile(
            TestFilePath("ihfokbkgjpifnbbojhneepfflplebdkc_1.crx"), &path));

        result.error = 0;
        result.response = path;
        result.downloaded_bytes = 53638;
        result.total_bytes = 53638;
      } else {
        NOTREACHED();
      }

      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE, base::Bind(&FakeCrxDownloader::OnDownloadProgress,
                                base::Unretained(this), result));

      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE,
          base::Bind(&FakeCrxDownloader::OnDownloadComplete,
                     base::Unretained(this), true, result, download_metrics));
    }
  };

  class FakePingManager : public FakePingManagerImpl {
   public:
    explicit FakePingManager(const scoped_refptr<Configurator>& config)
        : FakePingManagerImpl(config) {}
    ~FakePingManager() override {
      const auto ping_data = FakePingManagerImpl::ping_data();
      EXPECT_EQ(2u, ping_data.size());
      EXPECT_EQ("jebgalgnebhfojomionfpkfelancnnkf", ping_data[0].id);
      EXPECT_EQ(base::Version("0.9"), ping_data[0].previous_version);
      EXPECT_EQ(base::Version("1.0"), ping_data[0].next_version);
      EXPECT_EQ(0, ping_data[0].error_category);
      EXPECT_EQ(0, ping_data[0].error_code);
      EXPECT_EQ("ihfokbkgjpifnbbojhneepfflplebdkc", ping_data[1].id);
      EXPECT_EQ(base::Version("0.8"), ping_data[1].previous_version);
      EXPECT_EQ(base::Version("1.0"), ping_data[1].next_version);
      EXPECT_EQ(0, ping_data[1].error_category);
      EXPECT_EQ(0, ping_data[1].error_code);
    }
  };

  scoped_refptr<UpdateClient> update_client =
      base::MakeShared<UpdateClientImpl>(
          config(), base::MakeUnique<FakePingManager>(config()),
          &FakeUpdateChecker::Create, &FakeCrxDownloader::Create);

  MockObserver observer;
  {
    InSequence seq;
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_CHECKING_FOR_UPDATES,
                                  "jebgalgnebhfojomionfpkfelancnnkf")).Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_FOUND,
                                  "jebgalgnebhfojomionfpkfelancnnkf")).Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_DOWNLOADING,
                                  "jebgalgnebhfojomionfpkfelancnnkf"))
        .Times(AtLeast(1));
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_READY,
                                  "jebgalgnebhfojomionfpkfelancnnkf")).Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATED,
                                  "jebgalgnebhfojomionfpkfelancnnkf")).Times(1);
  }
  {
    InSequence seq;
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_CHECKING_FOR_UPDATES,
                                  "ihfokbkgjpifnbbojhneepfflplebdkc")).Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_FOUND,
                                  "ihfokbkgjpifnbbojhneepfflplebdkc")).Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_WAIT,
                                  "ihfokbkgjpifnbbojhneepfflplebdkc")).Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_DOWNLOADING,
                                  "ihfokbkgjpifnbbojhneepfflplebdkc"))
        .Times(AtLeast(1));
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_READY,
                                  "ihfokbkgjpifnbbojhneepfflplebdkc")).Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATED,
                                  "ihfokbkgjpifnbbojhneepfflplebdkc")).Times(1);
  }

  update_client->AddObserver(&observer);

  const std::vector<std::string> ids = {"jebgalgnebhfojomionfpkfelancnnkf",
                                        "ihfokbkgjpifnbbojhneepfflplebdkc"};
  update_client->Update(
      ids, base::Bind(&DataCallbackFake::Callback),
      base::Bind(&CompletionCallbackFake::Callback, quit_closure()));

  RunThreads();

  update_client->RemoveObserver(&observer);
}

// Tests the scenario where there is a download timeout for the first
// CRX. The update for the first CRX fails. The update client waits before
// attempting the update for the second CRX. This update succeeds.
TEST_F(UpdateClientTest, TwoCrxUpdateDownloadTimeout) {
  class DataCallbackFake {
   public:
    static void Callback(const std::vector<std::string>& ids,
                         std::vector<CrxComponent>* components) {
      CrxComponent crx1;
      crx1.name = "test_jebg";
      crx1.pk_hash.assign(jebg_hash, jebg_hash + arraysize(jebg_hash));
      crx1.version = base::Version("0.9");
      crx1.installer = base::MakeShared<TestInstaller>();

      CrxComponent crx2;
      crx2.name = "test_ihfo";
      crx2.pk_hash.assign(ihfo_hash, ihfo_hash + arraysize(ihfo_hash));
      crx2.version = base::Version("0.8");
      crx2.installer = base::MakeShared<TestInstaller>();

      components->push_back(crx1);
      components->push_back(crx2);
    }
  };

  class CompletionCallbackFake {
   public:
    static void Callback(const base::Closure& quit_closure, Error error) {
      EXPECT_EQ(Error::NONE, error);
      quit_closure.Run();
    }
  };

  class FakeUpdateChecker : public UpdateChecker {
   public:
    static std::unique_ptr<UpdateChecker> Create(
        const scoped_refptr<Configurator>& config,
        PersistedData* metadata) {
      return base::MakeUnique<FakeUpdateChecker>();
    }

    bool CheckForUpdates(
        const std::vector<std::string>& ids_to_check,
        const IdToComponentPtrMap& components,
        const std::string& additional_attributes,
        bool enabled_component_updates,
        const UpdateCheckCallback& update_check_callback) override {
      /*
      Fake the following response:

      <?xml version='1.0' encoding='UTF-8'?>
      <response protocol='3.0'>
        <app appid='jebgalgnebhfojomionfpkfelancnnkf'>
          <updatecheck status='ok'>
            <urls>
              <url codebase='http://localhost/download/'/>
            </urls>
            <manifest version='1.0' prodversionmin='11.0.1.0'>
              <packages>
                <package name='jebgalgnebhfojomionfpkfelancnnkf.crx'
                         hash_sha256='6fc4b93fd11134de1300c2c0bb88c12b644a4ec0fd
                                      7c9b12cb7cc067667bde87'/>
              </packages>
            </manifest>
          </updatecheck>
        </app>
        <app appid='ihfokbkgjpifnbbojhneepfflplebdkc'>
          <updatecheck status='ok'>
            <urls>
              <url codebase='http://localhost/download/'/>
            </urls>
            <manifest version='1.0' prodversionmin='11.0.1.0'>
              <packages>
                <package name='ihfokbkgjpifnbbojhneepfflplebdkc_1.crx'
                         hash_sha256='813c59747e139a608b3b5fc49633affc6db574373f
                                      309f156ea6d27229c0b3f9'/>
              </packages>
            </manifest>
          </updatecheck>
        </app>
      </response>
      */

      EXPECT_TRUE(enabled_component_updates);
      EXPECT_EQ(2u, ids_to_check.size());

      {
        const std::string id = "jebgalgnebhfojomionfpkfelancnnkf";
        EXPECT_EQ(id, ids_to_check[0]);
        EXPECT_EQ(1u, components.count(id));

        UpdateResponse::Result::Manifest::Package package;
        package.name = "jebgalgnebhfojomionfpkfelancnnkf.crx";
        package.hash_sha256 =
            "6fc4b93fd11134de1300c2c0bb88c12b644a4ec0fd7c9b12cb7cc067667bde87";

        UpdateResponse::Result result;
        result.extension_id = id;
        result.status = "ok";
        result.crx_urls.push_back(GURL("http://localhost/download/"));
        result.manifest.version = "1.0";
        result.manifest.browser_min_version = "11.0.1.0";
        result.manifest.packages.push_back(package);

        auto& component = components.at(id);
        component->SetParseResult(result);
      }

      {
        const std::string id = "ihfokbkgjpifnbbojhneepfflplebdkc";
        EXPECT_EQ(id, ids_to_check[1]);
        EXPECT_EQ(1u, components.count(id));

        UpdateResponse::Result::Manifest::Package package;
        package.name = "ihfokbkgjpifnbbojhneepfflplebdkc_1.crx";
        package.hash_sha256 =
            "813c59747e139a608b3b5fc49633affc6db574373f309f156ea6d27229c0b3f9";

        UpdateResponse::Result result;
        result.extension_id = id;
        result.status = "ok";
        result.crx_urls.push_back(GURL("http://localhost/download/"));
        result.manifest.version = "1.0";
        result.manifest.browser_min_version = "11.0.1.0";
        result.manifest.packages.push_back(package);

        auto& component = components.at(id);
        component->SetParseResult(result);
      }

      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE, base::Bind(update_check_callback, 0, 0));
      return true;
    }
  };

  class FakeCrxDownloader : public CrxDownloader {
   public:
    static std::unique_ptr<CrxDownloader> Create(
        bool is_background_download,
        net::URLRequestContextGetter* context_getter,
        const scoped_refptr<base::SequencedTaskRunner>& task_runner) {
      return base::MakeUnique<FakeCrxDownloader>();
    }

    FakeCrxDownloader()
        : CrxDownloader(base::ThreadTaskRunnerHandle::Get(), nullptr) {}

   private:
    void DoStartDownload(const GURL& url) override {
      DownloadMetrics download_metrics;
      FilePath path;
      Result result;
      if (url.path() == "/download/jebgalgnebhfojomionfpkfelancnnkf.crx") {
        download_metrics.url = url;
        download_metrics.downloader = DownloadMetrics::kNone;
        download_metrics.error = -118;
        download_metrics.downloaded_bytes = 0;
        download_metrics.total_bytes = 0;
        download_metrics.download_time_ms = 1000;

        EXPECT_TRUE(MakeTestFile(
            TestFilePath("jebgalgnebhfojomionfpkfelancnnkf.crx"), &path));

        result.error = -118;
        result.response = path;
        result.downloaded_bytes = 0;
        result.total_bytes = 0;
      } else if (url.path() ==
                 "/download/ihfokbkgjpifnbbojhneepfflplebdkc_1.crx") {
        download_metrics.url = url;
        download_metrics.downloader = DownloadMetrics::kNone;
        download_metrics.error = 0;
        download_metrics.downloaded_bytes = 53638;
        download_metrics.total_bytes = 53638;
        download_metrics.download_time_ms = 2000;

        EXPECT_TRUE(MakeTestFile(
            TestFilePath("ihfokbkgjpifnbbojhneepfflplebdkc_1.crx"), &path));

        result.error = 0;
        result.response = path;
        result.downloaded_bytes = 53638;
        result.total_bytes = 53638;
      } else {
        NOTREACHED();
      }

      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE, base::Bind(&FakeCrxDownloader::OnDownloadProgress,
                                base::Unretained(this), result));

      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE,
          base::Bind(&FakeCrxDownloader::OnDownloadComplete,
                     base::Unretained(this), true, result, download_metrics));
    }
  };

  class FakePingManager : public FakePingManagerImpl {
   public:
    explicit FakePingManager(const scoped_refptr<Configurator>& config)
        : FakePingManagerImpl(config) {}
    ~FakePingManager() override {
      const auto ping_data = FakePingManagerImpl::ping_data();
      EXPECT_EQ(2u, ping_data.size());
      EXPECT_EQ("jebgalgnebhfojomionfpkfelancnnkf", ping_data[0].id);
      EXPECT_EQ(base::Version("0.9"), ping_data[0].previous_version);
      EXPECT_EQ(base::Version("1.0"), ping_data[0].next_version);
      EXPECT_EQ(1, ping_data[0].error_category);
      EXPECT_EQ(-118, ping_data[0].error_code);
      EXPECT_EQ("ihfokbkgjpifnbbojhneepfflplebdkc", ping_data[1].id);
      EXPECT_EQ(base::Version("0.8"), ping_data[1].previous_version);
      EXPECT_EQ(base::Version("1.0"), ping_data[1].next_version);
      EXPECT_EQ(0, ping_data[1].error_category);
      EXPECT_EQ(0, ping_data[1].error_code);
    }
  };

  scoped_refptr<UpdateClient> update_client =
      base::MakeShared<UpdateClientImpl>(
          config(), base::MakeUnique<FakePingManager>(config()),
          &FakeUpdateChecker::Create, &FakeCrxDownloader::Create);

  MockObserver observer;
  {
    InSequence seq;
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_CHECKING_FOR_UPDATES,
                                  "jebgalgnebhfojomionfpkfelancnnkf")).Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_FOUND,
                                  "jebgalgnebhfojomionfpkfelancnnkf")).Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_DOWNLOADING,
                                  "jebgalgnebhfojomionfpkfelancnnkf"))
        .Times(AtLeast(1));
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_NOT_UPDATED,
                                  "jebgalgnebhfojomionfpkfelancnnkf")).Times(1);
  }
  {
    InSequence seq;
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_CHECKING_FOR_UPDATES,
                                  "ihfokbkgjpifnbbojhneepfflplebdkc")).Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_FOUND,
                                  "ihfokbkgjpifnbbojhneepfflplebdkc")).Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_WAIT,
                                  "ihfokbkgjpifnbbojhneepfflplebdkc")).Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_DOWNLOADING,
                                  "ihfokbkgjpifnbbojhneepfflplebdkc"))
        .Times(AtLeast(1));
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_READY,
                                  "ihfokbkgjpifnbbojhneepfflplebdkc")).Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATED,
                                  "ihfokbkgjpifnbbojhneepfflplebdkc")).Times(1);
  }

  update_client->AddObserver(&observer);

  const std::vector<std::string> ids = {"jebgalgnebhfojomionfpkfelancnnkf",
                                        "ihfokbkgjpifnbbojhneepfflplebdkc"};

  update_client->Update(
      ids, base::Bind(&DataCallbackFake::Callback),
      base::Bind(&CompletionCallbackFake::Callback, quit_closure()));

  RunThreads();

  update_client->RemoveObserver(&observer);
}

// Tests the differential update scenario for one CRX.
TEST_F(UpdateClientTest, OneCrxDiffUpdate) {
  class DataCallbackFake {
   public:
    static void Callback(const std::vector<std::string>& ids,
                         std::vector<CrxComponent>* components) {
      static int num_calls = 0;

      // Must use the same stateful installer object.
      static scoped_refptr<CrxInstaller> installer =
          base::MakeShared<VersionedTestInstaller>();

      ++num_calls;

      CrxComponent crx;
      crx.name = "test_ihfo";
      crx.pk_hash.assign(ihfo_hash, ihfo_hash + arraysize(ihfo_hash));
      crx.installer = installer;
      if (num_calls == 1) {
        crx.version = base::Version("0.8");
      } else if (num_calls == 2) {
        crx.version = base::Version("1.0");
      } else {
        NOTREACHED();
      }

      components->push_back(crx);
    }
  };

  class CompletionCallbackFake {
   public:
    static void Callback(const base::Closure& quit_closure, Error error) {
      EXPECT_EQ(Error::NONE, error);
      quit_closure.Run();
    }
  };

  class FakeUpdateChecker : public UpdateChecker {
   public:
    static std::unique_ptr<UpdateChecker> Create(
        const scoped_refptr<Configurator>& config,
        PersistedData* metadata) {
      return base::MakeUnique<FakeUpdateChecker>();
    }

    bool CheckForUpdates(
        const std::vector<std::string>& ids_to_check,
        const IdToComponentPtrMap& components,
        const std::string& additional_attributes,
        bool enabled_component_updates,
        const UpdateCheckCallback& update_check_callback) override {
      static int num_call = 0;
      ++num_call;

      UpdateResponse::Results results;

      if (num_call == 1) {
        /*
        Fake the following response:
        <?xml version='1.0' encoding='UTF-8'?>
        <response protocol='3.0'>
          <app appid='ihfokbkgjpifnbbojhneepfflplebdkc'>
            <updatecheck status='ok'>
              <urls>
                <url codebase='http://localhost/download/'/>
              </urls>
              <manifest version='1.0' prodversionmin='11.0.1.0'>
                <packages>
                  <package name='ihfokbkgjpifnbbojhneepfflplebdkc_1.crx'
                           hash_sha256='813c59747e139a608b3b5fc49633affc6db57437
                                        3f309f156ea6d27229c0b3f9'/>
                </packages>
              </manifest>
            </updatecheck>
          </app>
        </response>
        */
        const std::string id = "ihfokbkgjpifnbbojhneepfflplebdkc";
        EXPECT_EQ(id, ids_to_check[0]);
        EXPECT_EQ(1u, components.count(id));

        UpdateResponse::Result::Manifest::Package package;
        package.name = "ihfokbkgjpifnbbojhneepfflplebdkc_1.crx";
        package.hash_sha256 =
            "813c59747e139a608b3b5fc49633affc6db574373f309f156ea6d27229c0b3f9";

        UpdateResponse::Result result;
        result.extension_id = id;
        result.status = "ok";
        result.crx_urls.push_back(GURL("http://localhost/download/"));
        result.manifest.version = "1.0";
        result.manifest.browser_min_version = "11.0.1.0";
        result.manifest.packages.push_back(package);

        auto& component = components.at(id);
        component->SetParseResult(result);
      } else if (num_call == 2) {
        /*
        Fake the following response:
        <?xml version='1.0' encoding='UTF-8'?>
        <response protocol='3.0'>
          <app appid='ihfokbkgjpifnbbojhneepfflplebdkc'>
            <updatecheck status='ok'>
              <urls>
                <url codebase='http://localhost/download/'/>
                <url codebasediff='http://localhost/download/'/>
              </urls>
              <manifest version='2.0' prodversionmin='11.0.1.0'>
                <packages>
                  <package name='ihfokbkgjpifnbbojhneepfflplebdkc_2.crx'
                           namediff='ihfokbkgjpifnbbojhneepfflplebdkc_1to2.crx'
                           hash_sha256='1af337fbd19c72db0f870753bcd7711c3ae9dcaa
                                        0ecde26c262bad942b112990'
                           fp='22'
                           hashdiff_sha256='73c6e2d4f783fc4ca5481e89e0b8bfce7aec
                                            8ead3686290c94792658ec06f2f2'/>
                </packages>
              </manifest>
            </updatecheck>
          </app>
        </response>
        */
        const std::string id = "ihfokbkgjpifnbbojhneepfflplebdkc";
        EXPECT_EQ(id, ids_to_check[0]);
        EXPECT_EQ(1u, components.count(id));

        UpdateResponse::Result::Manifest::Package package;
        package.name = "ihfokbkgjpifnbbojhneepfflplebdkc_2.crx";
        package.namediff = "ihfokbkgjpifnbbojhneepfflplebdkc_1to2.crx";
        package.hash_sha256 =
            "1af337fbd19c72db0f870753bcd7711c3ae9dcaa0ecde26c262bad942b112990";
        package.hashdiff_sha256 =
            "73c6e2d4f783fc4ca5481e89e0b8bfce7aec8ead3686290c94792658ec06f2f2";
        package.fingerprint = "22";

        UpdateResponse::Result result;
        result.extension_id = id;
        result.status = "ok";
        result.crx_urls.push_back(GURL("http://localhost/download/"));
        result.crx_diffurls.push_back(GURL("http://localhost/download/"));
        result.manifest.version = "2.0";
        result.manifest.browser_min_version = "11.0.1.0";
        result.manifest.packages.push_back(package);

        auto& component = components.at(id);
        component->SetParseResult(result);
      } else {
        NOTREACHED();
      }

      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE, base::Bind(update_check_callback, 0, 0));
      return true;
    }
  };

  class FakeCrxDownloader : public CrxDownloader {
   public:
    static std::unique_ptr<CrxDownloader> Create(
        bool is_background_download,
        net::URLRequestContextGetter* context_getter,
        const scoped_refptr<base::SequencedTaskRunner>& task_runner) {
      return base::MakeUnique<FakeCrxDownloader>();
    }

    FakeCrxDownloader()
        : CrxDownloader(base::ThreadTaskRunnerHandle::Get(), nullptr) {}

   private:
    void DoStartDownload(const GURL& url) override {
      DownloadMetrics download_metrics;
      FilePath path;
      Result result;
      if (url.path() == "/download/ihfokbkgjpifnbbojhneepfflplebdkc_1.crx") {
        download_metrics.url = url;
        download_metrics.downloader = DownloadMetrics::kNone;
        download_metrics.error = 0;
        download_metrics.downloaded_bytes = 53638;
        download_metrics.total_bytes = 53638;
        download_metrics.download_time_ms = 2000;

        EXPECT_TRUE(MakeTestFile(
            TestFilePath("ihfokbkgjpifnbbojhneepfflplebdkc_1.crx"), &path));

        result.error = 0;
        result.response = path;
        result.downloaded_bytes = 53638;
        result.total_bytes = 53638;
      } else if (url.path() ==
                 "/download/ihfokbkgjpifnbbojhneepfflplebdkc_1to2.crx") {
        download_metrics.url = url;
        download_metrics.downloader = DownloadMetrics::kNone;
        download_metrics.error = 0;
        download_metrics.downloaded_bytes = 2105;
        download_metrics.total_bytes = 2105;
        download_metrics.download_time_ms = 1000;

        EXPECT_TRUE(MakeTestFile(
            TestFilePath("ihfokbkgjpifnbbojhneepfflplebdkc_1to2.crx"), &path));

        result.error = 0;
        result.response = path;
        result.downloaded_bytes = 2105;
        result.total_bytes = 2105;
      } else {
        NOTREACHED();
      }

      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE, base::Bind(&FakeCrxDownloader::OnDownloadProgress,
                                base::Unretained(this), result));

      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE,
          base::Bind(&FakeCrxDownloader::OnDownloadComplete,
                     base::Unretained(this), true, result, download_metrics));
    }
  };

  class FakePingManager : public FakePingManagerImpl {
   public:
    explicit FakePingManager(const scoped_refptr<Configurator>& config)
        : FakePingManagerImpl(config) {}
    ~FakePingManager() override {
      const auto ping_data = FakePingManagerImpl::ping_data();
      EXPECT_EQ(2u, ping_data.size());
      EXPECT_EQ("ihfokbkgjpifnbbojhneepfflplebdkc", ping_data[0].id);
      EXPECT_EQ(base::Version("0.8"), ping_data[0].previous_version);
      EXPECT_EQ(base::Version("1.0"), ping_data[0].next_version);
      EXPECT_EQ(0, ping_data[0].error_category);
      EXPECT_EQ(0, ping_data[0].error_code);
      EXPECT_EQ("ihfokbkgjpifnbbojhneepfflplebdkc", ping_data[1].id);
      EXPECT_EQ(base::Version("1.0"), ping_data[1].previous_version);
      EXPECT_EQ(base::Version("2.0"), ping_data[1].next_version);
      EXPECT_FALSE(ping_data[1].diff_update_failed);
      EXPECT_EQ(0, ping_data[1].diff_error_category);
      EXPECT_EQ(0, ping_data[1].diff_error_code);
      EXPECT_EQ(0, ping_data[1].error_category);
      EXPECT_EQ(0, ping_data[1].error_code);
    }
  };

  scoped_refptr<UpdateClient> update_client =
      base::MakeShared<UpdateClientImpl>(
          config(), base::MakeUnique<FakePingManager>(config()),
          &FakeUpdateChecker::Create, &FakeCrxDownloader::Create);

  MockObserver observer;
  {
    InSequence seq;
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_CHECKING_FOR_UPDATES,
                                  "ihfokbkgjpifnbbojhneepfflplebdkc")).Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_FOUND,
                                  "ihfokbkgjpifnbbojhneepfflplebdkc")).Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_DOWNLOADING,
                                  "ihfokbkgjpifnbbojhneepfflplebdkc"))
        .Times(AtLeast(1));
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_READY,
                                  "ihfokbkgjpifnbbojhneepfflplebdkc")).Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATED,
                                  "ihfokbkgjpifnbbojhneepfflplebdkc")).Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_CHECKING_FOR_UPDATES,
                                  "ihfokbkgjpifnbbojhneepfflplebdkc")).Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_FOUND,
                                  "ihfokbkgjpifnbbojhneepfflplebdkc")).Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_DOWNLOADING,
                                  "ihfokbkgjpifnbbojhneepfflplebdkc"))
        .Times(AtLeast(1));
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_READY,
                                  "ihfokbkgjpifnbbojhneepfflplebdkc")).Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATED,
                                  "ihfokbkgjpifnbbojhneepfflplebdkc")).Times(1);
  }

  update_client->AddObserver(&observer);

  const std::vector<std::string> ids = {"ihfokbkgjpifnbbojhneepfflplebdkc"};
  {
    base::RunLoop runloop;
    update_client->Update(
        ids, base::Bind(&DataCallbackFake::Callback),
        base::Bind(&CompletionCallbackFake::Callback, runloop.QuitClosure()));
    runloop.Run();
  }

  {
    base::RunLoop runloop;
    update_client->Update(
        ids, base::Bind(&DataCallbackFake::Callback),
        base::Bind(&CompletionCallbackFake::Callback, runloop.QuitClosure()));
    runloop.Run();
  }

  update_client->RemoveObserver(&observer);
}

// Tests the update scenario for one CRX where the CRX installer returns
// an error.
TEST_F(UpdateClientTest, OneCrxInstallError) {
  class MockInstaller : public CrxInstaller {
   public:
    MOCK_METHOD1(OnUpdateError, void(int error));
    MOCK_METHOD2(Install,
                 Result(const base::DictionaryValue& manifest,
                        const base::FilePath& unpack_path));
    MOCK_METHOD2(GetInstalledFile,
                 bool(const std::string& file, base::FilePath* installed_file));
    MOCK_METHOD0(Uninstall, bool());

    static void OnInstall(const base::DictionaryValue& manifest,
                          const base::FilePath& unpack_path) {
      base::DeleteFile(unpack_path, true);
    }

   protected:
    ~MockInstaller() override {}
  };

  class DataCallbackFake {
   public:
    static void Callback(const std::vector<std::string>& ids,
                         std::vector<CrxComponent>* components) {
      scoped_refptr<MockInstaller> installer =
          base::MakeShared<MockInstaller>();

      EXPECT_CALL(*installer, OnUpdateError(_)).Times(0);
      EXPECT_CALL(*installer, Install(_, _))
          .WillOnce(
              DoAll(Invoke(MockInstaller::OnInstall),
                    Return(CrxInstaller::Result(InstallError::GENERIC_ERROR))));
      EXPECT_CALL(*installer, GetInstalledFile(_, _)).Times(0);
      EXPECT_CALL(*installer, Uninstall()).Times(0);

      CrxComponent crx;
      crx.name = "test_jebg";
      crx.pk_hash.assign(jebg_hash, jebg_hash + arraysize(jebg_hash));
      crx.version = base::Version("0.9");
      crx.installer = installer;
      components->push_back(crx);
    }
  };

  class CompletionCallbackFake {
   public:
    static void Callback(const base::Closure& quit_closure, Error error) {
      EXPECT_EQ(Error::NONE, error);
      quit_closure.Run();
    }
  };

  class FakeUpdateChecker : public UpdateChecker {
   public:
    static std::unique_ptr<UpdateChecker> Create(
        const scoped_refptr<Configurator>& config,
        PersistedData* metadata) {
      return base::MakeUnique<FakeUpdateChecker>();
    }

    bool CheckForUpdates(
        const std::vector<std::string>& ids_to_check,
        const IdToComponentPtrMap& components,
        const std::string& additional_attributes,
        bool enabled_component_updates,
        const UpdateCheckCallback& update_check_callback) override {
      /*
      Fake the following response:

      <?xml version='1.0' encoding='UTF-8'?>
      <response protocol='3.0'>
        <app appid='jebgalgnebhfojomionfpkfelancnnkf'>
          <updatecheck status='ok'>
            <urls>
              <url codebase='http://localhost/download/'/>
            </urls>
            <manifest version='1.0' prodversionmin='11.0.1.0'>
              <packages>
                <package name='jebgalgnebhfojomionfpkfelancnnkf.crx'
                         hash_sha256='6fc4b93fd11134de1300c2c0bb88c12b644a4ec0fd
                                      7c9b12cb7cc067667bde87'/>
              </packages>
            </manifest>
          </updatecheck>
        </app>
      </response>
      */
      const std::string id = "jebgalgnebhfojomionfpkfelancnnkf";
      EXPECT_EQ(id, ids_to_check[0]);
      EXPECT_EQ(1u, components.count(id));

      UpdateResponse::Result::Manifest::Package package;
      package.name = "jebgalgnebhfojomionfpkfelancnnkf.crx";
      package.hash_sha256 =
          "6fc4b93fd11134de1300c2c0bb88c12b644a4ec0fd7c9b12cb7cc067667bde87";

      UpdateResponse::Result result;
      result.extension_id = id;
      result.status = "ok";
      result.crx_urls.push_back(GURL("http://localhost/download/"));
      result.manifest.version = "1.0";
      result.manifest.browser_min_version = "11.0.1.0";
      result.manifest.packages.push_back(package);

      auto& component = components.at(id);
      component->SetParseResult(result);

      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE, base::Bind(update_check_callback, 0, 0));
      return true;
    }
  };

  class FakeCrxDownloader : public CrxDownloader {
   public:
    static std::unique_ptr<CrxDownloader> Create(
        bool is_background_download,
        net::URLRequestContextGetter* context_getter,
        const scoped_refptr<base::SequencedTaskRunner>& task_runner) {
      return base::MakeUnique<FakeCrxDownloader>();
    }

    FakeCrxDownloader()
        : CrxDownloader(base::ThreadTaskRunnerHandle::Get(), nullptr) {}

   private:
    void DoStartDownload(const GURL& url) override {
      DownloadMetrics download_metrics;
      download_metrics.url = url;
      download_metrics.downloader = DownloadMetrics::kNone;
      download_metrics.error = 0;
      download_metrics.downloaded_bytes = 1843;
      download_metrics.total_bytes = 1843;
      download_metrics.download_time_ms = 1000;

      FilePath path;
      EXPECT_TRUE(MakeTestFile(
          TestFilePath("jebgalgnebhfojomionfpkfelancnnkf.crx"), &path));

      Result result;
      result.error = 0;
      result.response = path;
      result.downloaded_bytes = 1843;
      result.total_bytes = 1843;

      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE, base::Bind(&FakeCrxDownloader::OnDownloadProgress,
                                base::Unretained(this), result));

      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE,
          base::Bind(&FakeCrxDownloader::OnDownloadComplete,
                     base::Unretained(this), true, result, download_metrics));
    }
  };

  class FakePingManager : public FakePingManagerImpl {
   public:
    explicit FakePingManager(const scoped_refptr<Configurator>& config)
        : FakePingManagerImpl(config) {}
    ~FakePingManager() override {
      const auto ping_data = FakePingManagerImpl::ping_data();
      EXPECT_EQ(1u, ping_data.size());
      EXPECT_EQ("jebgalgnebhfojomionfpkfelancnnkf", ping_data[0].id);
      EXPECT_EQ(base::Version("0.9"), ping_data[0].previous_version);
      EXPECT_EQ(base::Version("1.0"), ping_data[0].next_version);
      EXPECT_EQ(3, ping_data[0].error_category);  // kInstallError.
      EXPECT_EQ(9, ping_data[0].error_code);      // kInstallerError.
    }
  };

  scoped_refptr<UpdateClient> update_client =
      base::MakeShared<UpdateClientImpl>(
          config(), base::MakeUnique<FakePingManager>(config()),
          &FakeUpdateChecker::Create, &FakeCrxDownloader::Create);

  MockObserver observer;
  {
    InSequence seq;
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_CHECKING_FOR_UPDATES,
                                  "jebgalgnebhfojomionfpkfelancnnkf")).Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_FOUND,
                                  "jebgalgnebhfojomionfpkfelancnnkf")).Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_DOWNLOADING,
                                  "jebgalgnebhfojomionfpkfelancnnkf"))
        .Times(AtLeast(1));
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_READY,
                                  "jebgalgnebhfojomionfpkfelancnnkf")).Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_NOT_UPDATED,
                                  "jebgalgnebhfojomionfpkfelancnnkf")).Times(1);
  }

  update_client->AddObserver(&observer);

  std::vector<std::string> ids = {"jebgalgnebhfojomionfpkfelancnnkf"};
  update_client->Update(
      ids, base::Bind(&DataCallbackFake::Callback),
      base::Bind(&CompletionCallbackFake::Callback, quit_closure()));

  RunThreads();

  update_client->RemoveObserver(&observer);
}

// Tests the fallback from differential to full update scenario for one CRX.
TEST_F(UpdateClientTest, OneCrxDiffUpdateFailsFullUpdateSucceeds) {
  class DataCallbackFake {
   public:
    static void Callback(const std::vector<std::string>& ids,
                         std::vector<CrxComponent>* components) {
      static int num_calls = 0;

      // Must use the same stateful installer object.
      static scoped_refptr<CrxInstaller> installer =
          base::MakeShared<VersionedTestInstaller>();

      ++num_calls;

      CrxComponent crx;
      crx.name = "test_ihfo";
      crx.pk_hash.assign(ihfo_hash, ihfo_hash + arraysize(ihfo_hash));
      crx.installer = installer;
      if (num_calls == 1) {
        crx.version = base::Version("0.8");
      } else if (num_calls == 2) {
        crx.version = base::Version("1.0");
      } else {
        NOTREACHED();
      }

      components->push_back(crx);
    }
  };

  class CompletionCallbackFake {
   public:
    static void Callback(const base::Closure& quit_closure, Error error) {
      EXPECT_EQ(Error::NONE, error);
      quit_closure.Run();
    }
  };

  class FakeUpdateChecker : public UpdateChecker {
   public:
    static std::unique_ptr<UpdateChecker> Create(
        const scoped_refptr<Configurator>& config,
        PersistedData* metadata) {
      return base::MakeUnique<FakeUpdateChecker>();
    }

    bool CheckForUpdates(
        const std::vector<std::string>& ids_to_check,
        const IdToComponentPtrMap& components,
        const std::string& additional_attributes,
        bool enabled_component_updates,
        const UpdateCheckCallback& update_check_callback) override {
      static int num_call = 0;
      ++num_call;

      UpdateResponse::Results results;

      if (num_call == 1) {
        /*
        Fake the following response:
        <?xml version='1.0' encoding='UTF-8'?>
        <response protocol='3.0'>
          <app appid='ihfokbkgjpifnbbojhneepfflplebdkc'>
            <updatecheck status='ok'>
              <urls>
                <url codebase='http://localhost/download/'/>
              </urls>
              <manifest version='1.0' prodversionmin='11.0.1.0'>
                <packages>
                  <package name='ihfokbkgjpifnbbojhneepfflplebdkc_1.crx'
                           hash_sha256='813c59747e139a608b3b5fc49633affc6db57437
                                        3f309f156ea6d27229c0b3f9'
                           fp='1'/>
                </packages>
              </manifest>
            </updatecheck>
          </app>
        </response>
        */
        const std::string id = "ihfokbkgjpifnbbojhneepfflplebdkc";
        EXPECT_EQ(id, ids_to_check[0]);
        EXPECT_EQ(1u, components.count(id));

        UpdateResponse::Result::Manifest::Package package;
        package.name = "ihfokbkgjpifnbbojhneepfflplebdkc_1.crx";
        package.hash_sha256 =
            "813c59747e139a608b3b5fc49633affc6db574373f309f156ea6d27229c0b3f9";
        package.fingerprint = "1";

        UpdateResponse::Result result;
        result.extension_id = id;
        result.status = "ok";
        result.crx_urls.push_back(GURL("http://localhost/download/"));
        result.manifest.version = "1.0";
        result.manifest.browser_min_version = "11.0.1.0";
        result.manifest.packages.push_back(package);

        auto& component = components.at(id);
        component->SetParseResult(result);
      } else if (num_call == 2) {
        /*
        Fake the following response:
        <?xml version='1.0' encoding='UTF-8'?>
        <response protocol='3.0'>
          <app appid='ihfokbkgjpifnbbojhneepfflplebdkc'>
            <updatecheck status='ok'>
              <urls>
                <url codebase='http://localhost/download/'/>
                <url codebasediff='http://localhost/download/'/>
              </urls>
              <manifest version='2.0' prodversionmin='11.0.1.0'>
                <packages>
                  <package name='ihfokbkgjpifnbbojhneepfflplebdkc_2.crx'
                           namediff='ihfokbkgjpifnbbojhneepfflplebdkc_1to2.crx'
                           hash_sha256='1af337fbd19c72db0f870753bcd7711c3ae9dcaa
                                        0ecde26c262bad942b112990'
                           fp='22'
                           hashdiff_sha256='73c6e2d4f783fc4ca5481e89e0b8bfce7aec
                                            8ead3686290c94792658ec06f2f2'/>
                </packages>
              </manifest>
            </updatecheck>
          </app>
        </response>
        */
        const std::string id = "ihfokbkgjpifnbbojhneepfflplebdkc";
        EXPECT_EQ(id, ids_to_check[0]);
        EXPECT_EQ(1u, components.count(id));

        UpdateResponse::Result::Manifest::Package package;
        package.name = "ihfokbkgjpifnbbojhneepfflplebdkc_2.crx";
        package.namediff = "ihfokbkgjpifnbbojhneepfflplebdkc_1to2.crx";
        package.hash_sha256 =
            "1af337fbd19c72db0f870753bcd7711c3ae9dcaa0ecde26c262bad942b112990";
        package.hashdiff_sha256 =
            "73c6e2d4f783fc4ca5481e89e0b8bfce7aec8ead3686290c94792658ec06f2f2";
        package.fingerprint = "22";

        UpdateResponse::Result result;
        result.extension_id = id;
        result.status = "ok";
        result.crx_urls.push_back(GURL("http://localhost/download/"));
        result.crx_diffurls.push_back(GURL("http://localhost/download/"));
        result.manifest.version = "2.0";
        result.manifest.browser_min_version = "11.0.1.0";
        result.manifest.packages.push_back(package);

        auto& component = components.at(id);
        component->SetParseResult(result);
      } else {
        NOTREACHED();
      }

      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE, base::Bind(update_check_callback, 0, 0));
      return true;
    }
  };

  class FakeCrxDownloader : public CrxDownloader {
   public:
    static std::unique_ptr<CrxDownloader> Create(
        bool is_background_download,
        net::URLRequestContextGetter* context_getter,
        const scoped_refptr<base::SequencedTaskRunner>& task_runner) {
      return base::MakeUnique<FakeCrxDownloader>();
    }

    FakeCrxDownloader()
        : CrxDownloader(base::ThreadTaskRunnerHandle::Get(), nullptr) {}

   private:
    void DoStartDownload(const GURL& url) override {
      DownloadMetrics download_metrics;
      FilePath path;
      Result result;
      if (url.path() == "/download/ihfokbkgjpifnbbojhneepfflplebdkc_1.crx") {
        download_metrics.url = url;
        download_metrics.downloader = DownloadMetrics::kNone;
        download_metrics.error = 0;
        download_metrics.downloaded_bytes = 53638;
        download_metrics.total_bytes = 53638;
        download_metrics.download_time_ms = 2000;

        EXPECT_TRUE(MakeTestFile(
            TestFilePath("ihfokbkgjpifnbbojhneepfflplebdkc_1.crx"), &path));

        result.error = 0;
        result.response = path;
        result.downloaded_bytes = 53638;
        result.total_bytes = 53638;
      } else if (url.path() ==
                 "/download/ihfokbkgjpifnbbojhneepfflplebdkc_1to2.crx") {
        // A download error is injected on this execution path.
        download_metrics.url = url;
        download_metrics.downloader = DownloadMetrics::kNone;
        download_metrics.error = -1;
        download_metrics.downloaded_bytes = 0;
        download_metrics.total_bytes = 2105;
        download_metrics.download_time_ms = 1000;

        EXPECT_TRUE(MakeTestFile(
            TestFilePath("ihfokbkgjpifnbbojhneepfflplebdkc_1to2.crx"), &path));

        result.error = -1;
        result.response = path;
        result.downloaded_bytes = 0;
        result.total_bytes = 2105;
      } else if (url.path() ==
                 "/download/ihfokbkgjpifnbbojhneepfflplebdkc_2.crx") {
        download_metrics.url = url;
        download_metrics.downloader = DownloadMetrics::kNone;
        download_metrics.error = 0;
        download_metrics.downloaded_bytes = 53855;
        download_metrics.total_bytes = 53855;
        download_metrics.download_time_ms = 1000;

        EXPECT_TRUE(MakeTestFile(
            TestFilePath("ihfokbkgjpifnbbojhneepfflplebdkc_2.crx"), &path));

        result.error = 0;
        result.response = path;
        result.downloaded_bytes = 53855;
        result.total_bytes = 53855;
      }

      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE, base::Bind(&FakeCrxDownloader::OnDownloadProgress,
                                base::Unretained(this), result));

      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE,
          base::Bind(&FakeCrxDownloader::OnDownloadComplete,
                     base::Unretained(this), true, result, download_metrics));
    }
  };

  class FakePingManager : public FakePingManagerImpl {
   public:
    explicit FakePingManager(const scoped_refptr<Configurator>& config)
        : FakePingManagerImpl(config) {}
    ~FakePingManager() override {
      const auto ping_data = FakePingManagerImpl::ping_data();
      EXPECT_EQ(2u, ping_data.size());
      EXPECT_EQ("ihfokbkgjpifnbbojhneepfflplebdkc", ping_data[0].id);
      EXPECT_EQ(base::Version("0.8"), ping_data[0].previous_version);
      EXPECT_EQ(base::Version("1.0"), ping_data[0].next_version);
      EXPECT_EQ(0, ping_data[0].error_category);
      EXPECT_EQ(0, ping_data[0].error_code);
      EXPECT_EQ("ihfokbkgjpifnbbojhneepfflplebdkc", ping_data[1].id);
      EXPECT_EQ(base::Version("1.0"), ping_data[1].previous_version);
      EXPECT_EQ(base::Version("2.0"), ping_data[1].next_version);
      EXPECT_EQ(0, ping_data[1].error_category);
      EXPECT_EQ(0, ping_data[1].error_code);
      EXPECT_TRUE(ping_data[1].diff_update_failed);
      EXPECT_EQ(1, ping_data[1].diff_error_category);  // kNetworkError.
      EXPECT_EQ(-1, ping_data[1].diff_error_code);
    }
  };

  scoped_refptr<UpdateClient> update_client =
      base::MakeShared<UpdateClientImpl>(
          config(), base::MakeUnique<FakePingManager>(config()),
          &FakeUpdateChecker::Create, &FakeCrxDownloader::Create);

  MockObserver observer;
  {
    InSequence seq;
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_CHECKING_FOR_UPDATES,
                                  "ihfokbkgjpifnbbojhneepfflplebdkc")).Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_FOUND,
                                  "ihfokbkgjpifnbbojhneepfflplebdkc")).Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_DOWNLOADING,
                                  "ihfokbkgjpifnbbojhneepfflplebdkc"))
        .Times(AtLeast(1));
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_READY,
                                  "ihfokbkgjpifnbbojhneepfflplebdkc")).Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATED,
                                  "ihfokbkgjpifnbbojhneepfflplebdkc")).Times(1);

    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_CHECKING_FOR_UPDATES,
                                  "ihfokbkgjpifnbbojhneepfflplebdkc")).Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_FOUND,
                                  "ihfokbkgjpifnbbojhneepfflplebdkc")).Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_DOWNLOADING,
                                  "ihfokbkgjpifnbbojhneepfflplebdkc"))
        .Times(AtLeast(1));
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_READY,
                                  "ihfokbkgjpifnbbojhneepfflplebdkc")).Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATED,
                                  "ihfokbkgjpifnbbojhneepfflplebdkc")).Times(1);
  }

  update_client->AddObserver(&observer);

  const std::vector<std::string> ids = {"ihfokbkgjpifnbbojhneepfflplebdkc"};

  {
    base::RunLoop runloop;
    update_client->Update(
        ids, base::Bind(&DataCallbackFake::Callback),
        base::Bind(&CompletionCallbackFake::Callback, runloop.QuitClosure()));
    runloop.Run();
  }

  {
    base::RunLoop runloop;
    update_client->Update(
        ids, base::Bind(&DataCallbackFake::Callback),
        base::Bind(&CompletionCallbackFake::Callback, runloop.QuitClosure()));
    runloop.Run();
  }

  update_client->RemoveObserver(&observer);
}

// Tests the queuing of update checks. In this scenario, two update checks are
// done for one CRX. The second update check call is queued up and will run
// after the first check has completed. The CRX has no updates.
TEST_F(UpdateClientTest, OneCrxNoUpdateQueuedCall) {
  class DataCallbackFake {
   public:
    static void Callback(const std::vector<std::string>& ids,
                         std::vector<CrxComponent>* components) {
      CrxComponent crx;
      crx.name = "test_jebg";
      crx.pk_hash.assign(jebg_hash, jebg_hash + arraysize(jebg_hash));
      crx.version = base::Version("0.9");
      crx.installer = base::MakeShared<TestInstaller>();
      components->push_back(crx);
    }
  };

  class CompletionCallbackFake {
   public:
    static void Callback(const base::Closure& quit_closure, Error error) {
      static int num_call = 0;
      ++num_call;

      EXPECT_EQ(Error::NONE, error);

      if (num_call == 2)
        quit_closure.Run();
    }
  };

  class FakeUpdateChecker : public UpdateChecker {
   public:
    static std::unique_ptr<UpdateChecker> Create(
        const scoped_refptr<Configurator>& config,
        PersistedData* metadata) {
      return base::MakeUnique<FakeUpdateChecker>();
    }

    bool CheckForUpdates(
        const std::vector<std::string>& ids_to_check,
        const IdToComponentPtrMap& components,
        const std::string& additional_attributes,
        bool enabled_component_updates,
        const UpdateCheckCallback& update_check_callback) override {
      EXPECT_TRUE(enabled_component_updates);
      EXPECT_EQ(1u, ids_to_check.size());
      const std::string id = "jebgalgnebhfojomionfpkfelancnnkf";
      EXPECT_EQ(id, ids_to_check.front());
      EXPECT_EQ(1u, components.count(id));

      auto& component = components.at(id);

      EXPECT_FALSE(component->on_demand());

      UpdateResponse::Result result;
      result.extension_id = id;
      result.status = "noupdate";
      component->SetParseResult(result);

      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE, base::Bind(update_check_callback, 0, 0));
      return true;
    }
  };

  class FakeCrxDownloader : public CrxDownloader {
   public:
    static std::unique_ptr<CrxDownloader> Create(
        bool is_background_download,
        net::URLRequestContextGetter* context_getter,
        const scoped_refptr<base::SequencedTaskRunner>& task_runner) {
      return base::MakeUnique<FakeCrxDownloader>();
    }

    FakeCrxDownloader()
        : CrxDownloader(base::ThreadTaskRunnerHandle::Get(), nullptr) {}

   private:
    void DoStartDownload(const GURL& url) override { EXPECT_TRUE(false); }
  };

  class FakePingManager : public FakePingManagerImpl {
   public:
    explicit FakePingManager(const scoped_refptr<Configurator>& config)
        : FakePingManagerImpl(config) {}
    ~FakePingManager() override { EXPECT_TRUE(ping_data().empty()); }
  };

  std::unique_ptr<PingManager> ping_manager =
      base::MakeUnique<FakePingManager>(config());
  scoped_refptr<UpdateClient> update_client =
      base::MakeShared<UpdateClientImpl>(
          config(), base::MakeUnique<FakePingManager>(config()),
          &FakeUpdateChecker::Create, &FakeCrxDownloader::Create);

  MockObserver observer;
  InSequence seq;
  EXPECT_CALL(observer, OnEvent(Events::COMPONENT_CHECKING_FOR_UPDATES,
                                "jebgalgnebhfojomionfpkfelancnnkf")).Times(1);
  EXPECT_CALL(observer, OnEvent(Events::COMPONENT_NOT_UPDATED,
                                "jebgalgnebhfojomionfpkfelancnnkf")).Times(1);
  EXPECT_CALL(observer, OnEvent(Events::COMPONENT_CHECKING_FOR_UPDATES,
                                "jebgalgnebhfojomionfpkfelancnnkf")).Times(1);
  EXPECT_CALL(observer, OnEvent(Events::COMPONENT_NOT_UPDATED,
                                "jebgalgnebhfojomionfpkfelancnnkf")).Times(1);

  update_client->AddObserver(&observer);

  const std::vector<std::string> ids = {"jebgalgnebhfojomionfpkfelancnnkf"};
  update_client->Update(
      ids, base::Bind(&DataCallbackFake::Callback),
      base::Bind(&CompletionCallbackFake::Callback, quit_closure()));
  update_client->Update(
      ids, base::Bind(&DataCallbackFake::Callback),
      base::Bind(&CompletionCallbackFake::Callback, quit_closure()));

  RunThreads();

  update_client->RemoveObserver(&observer);
}

// Tests the install of one CRX.
TEST_F(UpdateClientTest, OneCrxInstall) {
  class DataCallbackFake {
   public:
    static void Callback(const std::vector<std::string>& ids,
                         std::vector<CrxComponent>* components) {
      CrxComponent crx;
      crx.name = "test_jebg";
      crx.pk_hash.assign(jebg_hash, jebg_hash + arraysize(jebg_hash));
      crx.version = base::Version("0.0");
      crx.installer = base::MakeShared<TestInstaller>();

      components->push_back(crx);
    }
  };

  class CompletionCallbackFake {
   public:
    static void Callback(const base::Closure& quit_closure, Error error) {
      EXPECT_EQ(Error::NONE, error);
      quit_closure.Run();
    }
  };

  class FakeUpdateChecker : public UpdateChecker {
   public:
    static std::unique_ptr<UpdateChecker> Create(
        const scoped_refptr<Configurator>& config,
        PersistedData* metadata) {
      return base::MakeUnique<FakeUpdateChecker>();
    }

    bool CheckForUpdates(
        const std::vector<std::string>& ids_to_check,
        const IdToComponentPtrMap& components,
        const std::string& additional_attributes,
        bool enabled_component_updates,
        const UpdateCheckCallback& update_check_callback) override {
      /*
      Fake the following response:

      <?xml version='1.0' encoding='UTF-8'?>
      <response protocol='3.0'>
        <app appid='jebgalgnebhfojomionfpkfelancnnkf'>
          <updatecheck status='ok'>
            <urls>
              <url codebase='http://localhost/download/'/>
            </urls>
            <manifest version='1.0' prodversionmin='11.0.1.0'>
              <packages>
                <package name='jebgalgnebhfojomionfpkfelancnnkf.crx'
                         hash_sha256='6fc4b93fd11134de1300c2c0bb88c12b644a4ec0fd
                                      7c9b12cb7cc067667bde87'/>
              </packages>
            </manifest>
          </updatecheck>
        </app>
      </response>
      */
      EXPECT_TRUE(enabled_component_updates);
      EXPECT_EQ(1u, ids_to_check.size());

      const std::string id = "jebgalgnebhfojomionfpkfelancnnkf";
      EXPECT_EQ(id, ids_to_check[0]);
      EXPECT_EQ(1u, components.count(id));

      UpdateResponse::Result::Manifest::Package package;
      package.name = "jebgalgnebhfojomionfpkfelancnnkf.crx";
      package.hash_sha256 =
          "6fc4b93fd11134de1300c2c0bb88c12b644a4ec0fd7c9b12cb7cc067667bde87";

      UpdateResponse::Result result;
      result.extension_id = id;
      result.status = "ok";
      result.crx_urls.push_back(GURL("http://localhost/download/"));
      result.manifest.version = "1.0";
      result.manifest.browser_min_version = "11.0.1.0";
      result.manifest.packages.push_back(package);

      auto& component = components.at(id);
      component->SetParseResult(result);

      // Verify that calling Install sets ondemand.
      EXPECT_TRUE(component->on_demand());

      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE, base::Bind(update_check_callback, 0, 0));
      return true;
    }
  };

  class FakeCrxDownloader : public CrxDownloader {
   public:
    static std::unique_ptr<CrxDownloader> Create(
        bool is_background_download,
        net::URLRequestContextGetter* context_getter,
        const scoped_refptr<base::SequencedTaskRunner>& task_runner) {
      return base::MakeUnique<FakeCrxDownloader>();
    }

    FakeCrxDownloader()
        : CrxDownloader(base::ThreadTaskRunnerHandle::Get(), nullptr) {}

   private:
    void DoStartDownload(const GURL& url) override {
      DownloadMetrics download_metrics;
      FilePath path;
      Result result;
      if (url.path() == "/download/jebgalgnebhfojomionfpkfelancnnkf.crx") {
        download_metrics.url = url;
        download_metrics.downloader = DownloadMetrics::kNone;
        download_metrics.error = 0;
        download_metrics.downloaded_bytes = 1843;
        download_metrics.total_bytes = 1843;
        download_metrics.download_time_ms = 1000;

        EXPECT_TRUE(MakeTestFile(
            TestFilePath("jebgalgnebhfojomionfpkfelancnnkf.crx"), &path));

        result.error = 0;
        result.response = path;
        result.downloaded_bytes = 1843;
        result.total_bytes = 1843;
      } else {
        NOTREACHED();
      }

      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE, base::Bind(&FakeCrxDownloader::OnDownloadProgress,
                                base::Unretained(this), result));

      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE,
          base::Bind(&FakeCrxDownloader::OnDownloadComplete,
                     base::Unretained(this), true, result, download_metrics));
    }
  };

  class FakePingManager : public FakePingManagerImpl {
   public:
    explicit FakePingManager(const scoped_refptr<Configurator>& config)
        : FakePingManagerImpl(config) {}
    ~FakePingManager() override {
      const auto ping_data = FakePingManagerImpl::ping_data();
      EXPECT_EQ(1u, ping_data.size());
      EXPECT_EQ("jebgalgnebhfojomionfpkfelancnnkf", ping_data[0].id);
      EXPECT_EQ(base::Version("0.0"), ping_data[0].previous_version);
      EXPECT_EQ(base::Version("1.0"), ping_data[0].next_version);
      EXPECT_EQ(0, ping_data[0].error_category);
      EXPECT_EQ(0, ping_data[0].error_code);
    }
  };

  scoped_refptr<UpdateClient> update_client =
      base::MakeShared<UpdateClientImpl>(
          config(), base::MakeUnique<FakePingManager>(config()),
          &FakeUpdateChecker::Create, &FakeCrxDownloader::Create);

  MockObserver observer;
  InSequence seq;
  EXPECT_CALL(observer, OnEvent(Events::COMPONENT_CHECKING_FOR_UPDATES,
                                "jebgalgnebhfojomionfpkfelancnnkf")).Times(1);
  EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_FOUND,
                                "jebgalgnebhfojomionfpkfelancnnkf")).Times(1);
  EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_DOWNLOADING,
                                "jebgalgnebhfojomionfpkfelancnnkf"))
      .Times(AtLeast(1));
  EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_READY,
                                "jebgalgnebhfojomionfpkfelancnnkf")).Times(1);
  EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATED,
                                "jebgalgnebhfojomionfpkfelancnnkf")).Times(1);

  update_client->AddObserver(&observer);

  update_client->Install(
      std::string("jebgalgnebhfojomionfpkfelancnnkf"),
      base::Bind(&DataCallbackFake::Callback),
      base::Bind(&CompletionCallbackFake::Callback, quit_closure()));

  RunThreads();

  update_client->RemoveObserver(&observer);
}

// Tests that overlapping installs of the same CRX result in an error.
TEST_F(UpdateClientTest, ConcurrentInstallSameCRX) {
  class DataCallbackFake {
   public:
    static void Callback(const std::vector<std::string>& ids,
                         std::vector<CrxComponent>* components) {
      CrxComponent crx;
      crx.name = "test_jebg";
      crx.pk_hash.assign(jebg_hash, jebg_hash + arraysize(jebg_hash));
      crx.version = base::Version("0.0");
      crx.installer = base::MakeShared<TestInstaller>();

      components->push_back(crx);
    }
  };

  class CompletionCallbackFake {
   public:
    static void Callback(const base::Closure& quit_closure, Error error) {
      static int num_call = 0;
      ++num_call;

      EXPECT_LE(num_call, 2);

      if (num_call == 1) {
        EXPECT_EQ(Error::UPDATE_IN_PROGRESS, error);
        return;
      }
      if (num_call == 2) {
        EXPECT_EQ(Error::NONE, error);
        quit_closure.Run();
      }
    }
  };

  class FakeUpdateChecker : public UpdateChecker {
   public:
    static std::unique_ptr<UpdateChecker> Create(
        const scoped_refptr<Configurator>& config,
        PersistedData* metadata) {
      return base::MakeUnique<FakeUpdateChecker>();
    }

    bool CheckForUpdates(
        const std::vector<std::string>& ids_to_check,
        const IdToComponentPtrMap& components,
        const std::string& additional_attributes,
        bool enabled_component_updates,
        const UpdateCheckCallback& update_check_callback) override {
      EXPECT_TRUE(enabled_component_updates);
      EXPECT_EQ(1u, ids_to_check.size());
      const std::string id = "jebgalgnebhfojomionfpkfelancnnkf";
      EXPECT_EQ(id, ids_to_check.front());
      EXPECT_EQ(1u, components.count(id));

      UpdateResponse::Result result;
      result.extension_id = id;
      result.status = "noupdate";

      auto& component = components.at(id);
      component->SetParseResult(result);

      // Verify that calling Install sets ondemand.
      EXPECT_TRUE(component->on_demand());

      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE, base::Bind(update_check_callback, 0, 0));
      return true;
    }
  };

  class FakeCrxDownloader : public CrxDownloader {
   public:
    static std::unique_ptr<CrxDownloader> Create(
        bool is_background_download,
        net::URLRequestContextGetter* context_getter,
        const scoped_refptr<base::SequencedTaskRunner>& task_runner) {
      return base::MakeUnique<FakeCrxDownloader>();
    }

    FakeCrxDownloader()
        : CrxDownloader(base::ThreadTaskRunnerHandle::Get(), nullptr) {}

   private:
    void DoStartDownload(const GURL& url) override { EXPECT_TRUE(false); }
  };

  class FakePingManager : public FakePingManagerImpl {
   public:
    explicit FakePingManager(const scoped_refptr<Configurator>& config)
        : FakePingManagerImpl(config) {}
    ~FakePingManager() override { EXPECT_TRUE(ping_data().empty()); }
  };

  std::unique_ptr<FakePingManager> ping_manager =
      base::MakeUnique<FakePingManager>(config());
  scoped_refptr<UpdateClient> update_client =
      base::MakeShared<UpdateClientImpl>(
          config(), base::MakeUnique<FakePingManager>(config()),
          &FakeUpdateChecker::Create, &FakeCrxDownloader::Create);

  MockObserver observer;
  EXPECT_CALL(observer, OnEvent(Events::COMPONENT_CHECKING_FOR_UPDATES,
                                "jebgalgnebhfojomionfpkfelancnnkf"))
      .Times(1);
  EXPECT_CALL(observer, OnEvent(Events::COMPONENT_NOT_UPDATED,
                                "jebgalgnebhfojomionfpkfelancnnkf"))
      .Times(1);

  update_client->AddObserver(&observer);

  update_client->Install(
      std::string("jebgalgnebhfojomionfpkfelancnnkf"),
      base::Bind(&DataCallbackFake::Callback),
      base::Bind(&CompletionCallbackFake::Callback, quit_closure()));

  update_client->Install(
      std::string("jebgalgnebhfojomionfpkfelancnnkf"),
      base::Bind(&DataCallbackFake::Callback),
      base::Bind(&CompletionCallbackFake::Callback, quit_closure()));

  RunThreads();

  update_client->RemoveObserver(&observer);
}

// Tests that UpdateClient::Update returns Error::INVALID_ARGUMENT when
// the |ids| parameter is empty.
TEST_F(UpdateClientTest, EmptyIdList) {
  class DataCallbackFake {
   public:
    static void Callback(const std::vector<std::string>& ids,
                         std::vector<CrxComponent>* components) {}
  };

  class CompletionCallbackFake {
   public:
    static void Callback(const base::Closure& quit_closure, Error error) {
      DCHECK_EQ(Error::INVALID_ARGUMENT, error);
      quit_closure.Run();
    }
  };

  class FakeUpdateChecker : public UpdateChecker {
   public:
    static std::unique_ptr<UpdateChecker> Create(
        const scoped_refptr<Configurator>& config,
        PersistedData* metadata) {
      return base::MakeUnique<FakeUpdateChecker>();
    }

    bool CheckForUpdates(
        const std::vector<std::string>& ids_to_check,
        const IdToComponentPtrMap& components,
        const std::string& additional_attributes,
        bool enabled_component_updates,
        const UpdateCheckCallback& update_check_callback) override {
      return false;
    }
  };

  class FakeCrxDownloader : public CrxDownloader {
   public:
    static std::unique_ptr<CrxDownloader> Create(
        bool is_background_download,
        net::URLRequestContextGetter* context_getter,
        const scoped_refptr<base::SequencedTaskRunner>& task_runner) {
      return base::MakeUnique<FakeCrxDownloader>();
    }

    FakeCrxDownloader()
        : CrxDownloader(base::ThreadTaskRunnerHandle::Get(), nullptr) {}

   private:
    void DoStartDownload(const GURL& url) override { EXPECT_TRUE(false); }
  };

  scoped_refptr<UpdateClient> update_client =
      base::MakeShared<UpdateClientImpl>(
          config(), base::MakeUnique<FakePingManagerImpl>(config()),
          &FakeUpdateChecker::Create, &FakeCrxDownloader::Create);

  const std::vector<std::string> empty_id_list;
  update_client->Update(
      empty_id_list, base::Bind(&DataCallbackFake::Callback),
      base::Bind(&CompletionCallbackFake::Callback, quit_closure()));
  RunThreads();
}

TEST_F(UpdateClientTest, SendUninstallPing) {
  class CompletionCallbackFake {
   public:
    static void Callback(const base::Closure& quit_closure, Error error) {
      quit_closure.Run();
    }
  };

  class FakeUpdateChecker : public UpdateChecker {
   public:
    static std::unique_ptr<UpdateChecker> Create(
        const scoped_refptr<Configurator>& config,
        PersistedData* metadata) {
      return nullptr;
    }

    bool CheckForUpdates(
        const std::vector<std::string>& ids_to_check,
        const IdToComponentPtrMap& components,
        const std::string& additional_attributes,
        bool enabled_component_updates,
        const UpdateCheckCallback& update_check_callback) override {
      return false;
    }
  };

  class FakeCrxDownloader : public CrxDownloader {
   public:
    static std::unique_ptr<CrxDownloader> Create(
        bool is_background_download,
        net::URLRequestContextGetter* context_getter,
        const scoped_refptr<base::SequencedTaskRunner>& task_runner) {
      return nullptr;
    }

   private:
    FakeCrxDownloader()
        : CrxDownloader(base::ThreadTaskRunnerHandle::Get(), nullptr) {}
    ~FakeCrxDownloader() override {}

    void DoStartDownload(const GURL& url) override {}
  };

  class FakePingManager : public FakePingManagerImpl {
   public:
    explicit FakePingManager(const scoped_refptr<Configurator>& config)
        : FakePingManagerImpl(config) {}
    ~FakePingManager() override {
      const auto ping_data = FakePingManagerImpl::ping_data();
      EXPECT_EQ(1u, ping_data.size());
      EXPECT_EQ("jebgalgnebhfojomionfpkfelancnnkf", ping_data[0].id);
      EXPECT_EQ(base::Version("1.0"), ping_data[0].previous_version);
      EXPECT_EQ(base::Version("0.0"), ping_data[0].next_version);
      EXPECT_EQ(10, ping_data[0].extra_code1);
    }
  };

  scoped_refptr<UpdateClient> update_client =
      base::MakeShared<UpdateClientImpl>(
          config(), base::MakeUnique<FakePingManager>(config()),
          &FakeUpdateChecker::Create, &FakeCrxDownloader::Create);

  update_client->SendUninstallPing(
      "jebgalgnebhfojomionfpkfelancnnkf", base::Version("1.0"), 10,
      base::Bind(&CompletionCallbackFake::Callback, quit_closure()));

  RunThreads();
}

TEST_F(UpdateClientTest, RetryAfter) {
  class DataCallbackFake {
   public:
    static void Callback(const std::vector<std::string>& ids,
                         std::vector<CrxComponent>* components) {
      CrxComponent crx;
      crx.name = "test_jebg";
      crx.pk_hash.assign(jebg_hash, jebg_hash + arraysize(jebg_hash));
      crx.version = base::Version("0.9");
      crx.installer = base::MakeShared<TestInstaller>();
      components->push_back(crx);
    }
  };

  class CompletionCallbackFake {
   public:
    static void Callback(const base::Closure& quit_closure, Error error) {
      static int num_call = 0;
      ++num_call;

      EXPECT_LE(num_call, 4);

      if (num_call == 1) {
        EXPECT_EQ(Error::NONE, error);
      } else if (num_call == 2) {
        // This request is throttled since the update engine received a
        // positive |retry_after_sec| value in the update check response.
        EXPECT_EQ(Error::RETRY_LATER, error);
      } else if (num_call == 3) {
        // This request is a foreground Install, which is never throttled.
        // The update engine received a |retry_after_sec| value of 0, which
        // resets the throttling.
        EXPECT_EQ(Error::NONE, error);
      } else if (num_call == 4) {
        // This request succeeds since there is no throttling in effect.
        EXPECT_EQ(Error::NONE, error);
      }

      quit_closure.Run();
    }
  };

  class FakeUpdateChecker : public UpdateChecker {
   public:
    static std::unique_ptr<UpdateChecker> Create(
        const scoped_refptr<Configurator>& config,
        PersistedData* metadata) {
      return base::MakeUnique<FakeUpdateChecker>();
    }

    bool CheckForUpdates(
        const std::vector<std::string>& ids_to_check,
        const IdToComponentPtrMap& components,
        const std::string& additional_attributes,
        bool enabled_component_updates,
        const UpdateCheckCallback& update_check_callback) override {
      static int num_call = 0;
      ++num_call;

      EXPECT_LE(num_call, 3);

      int retry_after_sec(0);
      if (num_call == 1) {
        // Throttle the next call.
        retry_after_sec = 60 * 60;  // 1 hour.
      }

      EXPECT_EQ(1u, ids_to_check.size());
      const std::string id = "jebgalgnebhfojomionfpkfelancnnkf";
      EXPECT_EQ(id, ids_to_check.front());
      EXPECT_EQ(1u, components.count(id));

      auto& component = components.at(id);

      UpdateResponse::Result result;
      result.extension_id = id;
      result.status = "noupdate";
      component->SetParseResult(result);

      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE, base::Bind(update_check_callback, 0, retry_after_sec));
      return true;
    }
  };

  class FakeCrxDownloader : public CrxDownloader {
   public:
    static std::unique_ptr<CrxDownloader> Create(
        bool is_background_download,
        net::URLRequestContextGetter* context_getter,
        const scoped_refptr<base::SequencedTaskRunner>& task_runner) {
      return base::MakeUnique<FakeCrxDownloader>();
    }

    FakeCrxDownloader()
        : CrxDownloader(base::ThreadTaskRunnerHandle::Get(), nullptr) {}

   private:
    void DoStartDownload(const GURL& url) override { EXPECT_TRUE(false); }
  };

  class FakePingManager : public FakePingManagerImpl {
   public:
    explicit FakePingManager(const scoped_refptr<Configurator>& config)
        : FakePingManagerImpl(config) {}
    ~FakePingManager() override { EXPECT_TRUE(ping_data().empty()); }
  };

  scoped_refptr<UpdateClient> update_client =
      base::MakeShared<UpdateClientImpl>(
          config(), base::MakeUnique<FakePingManager>(config()),
          &FakeUpdateChecker::Create, &FakeCrxDownloader::Create);

  MockObserver observer;

  InSequence seq;
  EXPECT_CALL(observer, OnEvent(Events::COMPONENT_CHECKING_FOR_UPDATES,
                                "jebgalgnebhfojomionfpkfelancnnkf"))
      .Times(1);
  EXPECT_CALL(observer, OnEvent(Events::COMPONENT_NOT_UPDATED,
                                "jebgalgnebhfojomionfpkfelancnnkf"))
      .Times(1);
  EXPECT_CALL(observer, OnEvent(Events::COMPONENT_CHECKING_FOR_UPDATES,
                                "jebgalgnebhfojomionfpkfelancnnkf"))
      .Times(1);
  EXPECT_CALL(observer, OnEvent(Events::COMPONENT_NOT_UPDATED,
                                "jebgalgnebhfojomionfpkfelancnnkf"))
      .Times(1);
  EXPECT_CALL(observer, OnEvent(Events::COMPONENT_CHECKING_FOR_UPDATES,
                                "jebgalgnebhfojomionfpkfelancnnkf"))
      .Times(1);
  EXPECT_CALL(observer, OnEvent(Events::COMPONENT_NOT_UPDATED,
                                "jebgalgnebhfojomionfpkfelancnnkf"))
      .Times(1);

  update_client->AddObserver(&observer);

  const std::vector<std::string> ids = {"jebgalgnebhfojomionfpkfelancnnkf"};
  {
    // The engine handles this Update call but responds with a valid
    // |retry_after_sec|, which causes subsequent calls to fail.
    base::RunLoop runloop;
    update_client->Update(
        ids, base::Bind(&DataCallbackFake::Callback),
        base::Bind(&CompletionCallbackFake::Callback, runloop.QuitClosure()));
    runloop.Run();
  }

  {
    // This call will result in a completion callback invoked with
    // Error::ERROR_UPDATE_RETRY_LATER.
    base::RunLoop runloop;
    update_client->Update(
        ids, base::Bind(&DataCallbackFake::Callback),
        base::Bind(&CompletionCallbackFake::Callback, runloop.QuitClosure()));
    runloop.Run();
  }

  {
    // The Install call is handled, and the throttling is reset due to
    // the value of |retry_after_sec| in the completion callback.
    base::RunLoop runloop;
    update_client->Install(
        std::string("jebgalgnebhfojomionfpkfelancnnkf"),
        base::Bind(&DataCallbackFake::Callback),
        base::Bind(&CompletionCallbackFake::Callback, runloop.QuitClosure()));
    runloop.Run();
  }

  {
    // This call succeeds.
    base::RunLoop runloop;
    update_client->Update(
        ids, base::Bind(&DataCallbackFake::Callback),
        base::Bind(&CompletionCallbackFake::Callback, runloop.QuitClosure()));
    runloop.Run();
  }

  update_client->RemoveObserver(&observer);
}

// Tests the update check for two CRXs scenario. The first component supports
// the group policy to enable updates, and has its updates disabled. The second
// component has an update. The server does not honor the "updatedisabled"
// attribute and returns updates for both components. However, the update for
// the first component is not apply and the client responds with a
// (SERVICE_ERROR, UPDATE_DISABLED)
TEST_F(UpdateClientTest, TwoCrxUpdateOneUpdateDisabled) {
  class DataCallbackFake {
   public:
    static void Callback(const std::vector<std::string>& ids,
                         std::vector<CrxComponent>* components) {
      CrxComponent crx1;
      crx1.name = "test_jebg";
      crx1.pk_hash.assign(jebg_hash, jebg_hash + arraysize(jebg_hash));
      crx1.version = base::Version("0.9");
      crx1.installer = base::MakeShared<TestInstaller>();
      crx1.supports_group_policy_enable_component_updates = true;

      CrxComponent crx2;
      crx2.name = "test_ihfo";
      crx2.pk_hash.assign(ihfo_hash, ihfo_hash + arraysize(ihfo_hash));
      crx2.version = base::Version("0.8");
      crx2.installer = base::MakeShared<TestInstaller>();

      components->push_back(crx1);
      components->push_back(crx2);
    }
  };

  class CompletionCallbackFake {
   public:
    static void Callback(const base::Closure& quit_closure, Error error) {
      EXPECT_EQ(Error::NONE, error);
      quit_closure.Run();
    }
  };

  class FakeUpdateChecker : public UpdateChecker {
   public:
    static std::unique_ptr<UpdateChecker> Create(
        const scoped_refptr<Configurator>& config,
        PersistedData* metadata) {
      return base::MakeUnique<FakeUpdateChecker>();
    }

    bool CheckForUpdates(
        const std::vector<std::string>& ids_to_check,
        const IdToComponentPtrMap& components,
        const std::string& additional_attributes,
        bool enabled_component_updates,
        const UpdateCheckCallback& update_check_callback) override {
      /*
      Fake the following response:

      <?xml version='1.0' encoding='UTF-8'?>
      <response protocol='3.0'>
        <app appid='jebgalgnebhfojomionfpkfelancnnkf'>
          <updatecheck status='ok'>
            <urls>
              <url codebase='http://localhost/download/'/>
            </urls>
            <manifest version='1.0' prodversionmin='11.0.1.0'>
              <packages>
                <package name='jebgalgnebhfojomionfpkfelancnnkf.crx'
                         hash_sha256='6fc4b93fd11134de1300c2c0bb88c12b644a4ec0fd
                                      7c9b12cb7cc067667bde87'/>
              </packages>
            </manifest>
          </updatecheck>
        </app>
        <app appid='ihfokbkgjpifnbbojhneepfflplebdkc'>
          <updatecheck status='ok'>
            <urls>
              <url codebase='http://localhost/download/'/>
            </urls>
            <manifest version='1.0' prodversionmin='11.0.1.0'>
              <packages>
                <package name='ihfokbkgjpifnbbojhneepfflplebdkc_1.crx'
                         hash_sha256='813c59747e139a608b3b5fc49633affc6db574373f
                                      309f156ea6d27229c0b3f9'/>
              </packages>
            </manifest>
          </updatecheck>
        </app>
      </response>
      */

      // UpdateClient reads the state of |enabled_component_updates| from the
      // configurator instance, persists its value in the corresponding
      // update context, and propagates it down to each of the update actions,
      // and further down to the UpdateChecker instance.
      EXPECT_FALSE(enabled_component_updates);

      EXPECT_EQ(2u, ids_to_check.size());

      {
        const std::string id = "jebgalgnebhfojomionfpkfelancnnkf";
        EXPECT_EQ(id, ids_to_check[0]);
        EXPECT_EQ(1u, components.count(id));

        UpdateResponse::Result::Manifest::Package package;
        package.name = "jebgalgnebhfojomionfpkfelancnnkf.crx";
        package.hash_sha256 =
            "6fc4b93fd11134de1300c2c0bb88c12b644a4ec0fd7c9b12cb7cc067667bde87";

        UpdateResponse::Result result;
        result.extension_id = id;
        result.status = "ok";
        result.crx_urls.push_back(GURL("http://localhost/download/"));
        result.manifest.version = "1.0";
        result.manifest.browser_min_version = "11.0.1.0";
        result.manifest.packages.push_back(package);

        auto& component = components.at(id);
        component->SetParseResult(result);
      }

      {
        const std::string id = "ihfokbkgjpifnbbojhneepfflplebdkc";
        EXPECT_EQ(id, ids_to_check[1]);
        EXPECT_EQ(1u, components.count(id));

        UpdateResponse::Result::Manifest::Package package;
        package.name = "ihfokbkgjpifnbbojhneepfflplebdkc_1.crx";
        package.hash_sha256 =
            "813c59747e139a608b3b5fc49633affc6db574373f309f156ea6d27229c0b3f9";

        UpdateResponse::Result result;
        result.extension_id = id;
        result.status = "ok";
        result.crx_urls.push_back(GURL("http://localhost/download/"));
        result.manifest.version = "1.0";
        result.manifest.browser_min_version = "11.0.1.0";
        result.manifest.packages.push_back(package);

        auto& component = components.at(id);
        component->SetParseResult(result);
      }

      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE, base::Bind(update_check_callback, 0, 0));
      return true;
    }
  };

  class FakeCrxDownloader : public CrxDownloader {
   public:
    static std::unique_ptr<CrxDownloader> Create(
        bool is_background_download,
        net::URLRequestContextGetter* context_getter,
        const scoped_refptr<base::SequencedTaskRunner>& task_runner) {
      return base::MakeUnique<FakeCrxDownloader>();
    }

    FakeCrxDownloader()
        : CrxDownloader(base::ThreadTaskRunnerHandle::Get(), nullptr) {}

   private:
    void DoStartDownload(const GURL& url) override {
      DownloadMetrics download_metrics;
      FilePath path;
      Result result;
      if (url.path() == "/download/ihfokbkgjpifnbbojhneepfflplebdkc_1.crx") {
        download_metrics.url = url;
        download_metrics.downloader = DownloadMetrics::kNone;
        download_metrics.error = 0;
        download_metrics.downloaded_bytes = 53638;
        download_metrics.total_bytes = 53638;
        download_metrics.download_time_ms = 2000;

        EXPECT_TRUE(MakeTestFile(
            TestFilePath("ihfokbkgjpifnbbojhneepfflplebdkc_1.crx"), &path));

        result.error = 0;
        result.response = path;
        result.downloaded_bytes = 53638;
        result.total_bytes = 53638;
      } else {
        NOTREACHED();
      }

      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE, base::Bind(&FakeCrxDownloader::OnDownloadProgress,
                                base::Unretained(this), result));

      base::ThreadTaskRunnerHandle::Get()->PostTask(
          FROM_HERE,
          base::Bind(&FakeCrxDownloader::OnDownloadComplete,
                     base::Unretained(this), true, result, download_metrics));
    }
  };

  class FakePingManager : public FakePingManagerImpl {
   public:
    explicit FakePingManager(const scoped_refptr<Configurator>& config)
        : FakePingManagerImpl(config) {}
    ~FakePingManager() override {
      const auto ping_data = FakePingManagerImpl::ping_data();
      EXPECT_EQ(2u, ping_data.size());
      EXPECT_EQ("jebgalgnebhfojomionfpkfelancnnkf", ping_data[0].id);
      EXPECT_EQ(base::Version("0.9"), ping_data[0].previous_version);
      EXPECT_EQ(base::Version("1.0"), ping_data[0].next_version);
      EXPECT_EQ(4, ping_data[0].error_category);
      EXPECT_EQ(2, ping_data[0].error_code);
      EXPECT_EQ("ihfokbkgjpifnbbojhneepfflplebdkc", ping_data[1].id);
      EXPECT_EQ(base::Version("0.8"), ping_data[1].previous_version);
      EXPECT_EQ(base::Version("1.0"), ping_data[1].next_version);
      EXPECT_EQ(0, ping_data[1].error_category);
      EXPECT_EQ(0, ping_data[1].error_code);
    }
  };

  // Disables updates for the components declaring support for the group policy.
  config()->SetEnabledComponentUpdates(false);
  scoped_refptr<UpdateClient> update_client =
      base::MakeShared<UpdateClientImpl>(
          config(), base::MakeUnique<FakePingManager>(config()),
          &FakeUpdateChecker::Create, &FakeCrxDownloader::Create);

  MockObserver observer;
  {
    InSequence seq;
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_CHECKING_FOR_UPDATES,
                                  "jebgalgnebhfojomionfpkfelancnnkf"))
        .Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_FOUND,
                                  "jebgalgnebhfojomionfpkfelancnnkf"))
        .Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_NOT_UPDATED,
                                  "jebgalgnebhfojomionfpkfelancnnkf"))
        .Times(1);
  }
  {
    InSequence seq;
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_CHECKING_FOR_UPDATES,
                                  "ihfokbkgjpifnbbojhneepfflplebdkc"))
        .Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_FOUND,
                                  "ihfokbkgjpifnbbojhneepfflplebdkc"))
        .Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_DOWNLOADING,
                                  "ihfokbkgjpifnbbojhneepfflplebdkc"))
        .Times(AtLeast(1));
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATE_READY,
                                  "ihfokbkgjpifnbbojhneepfflplebdkc"))
        .Times(1);
    EXPECT_CALL(observer, OnEvent(Events::COMPONENT_UPDATED,
                                  "ihfokbkgjpifnbbojhneepfflplebdkc"))
        .Times(1);
  }

  update_client->AddObserver(&observer);

  const std::vector<std::string> ids = {"jebgalgnebhfojomionfpkfelancnnkf",
                                        "ihfokbkgjpifnbbojhneepfflplebdkc"};
  update_client->Update(
      ids, base::Bind(&DataCallbackFake::Callback),
      base::Bind(&CompletionCallbackFake::Callback, quit_closure()));

  RunThreads();

  update_client->RemoveObserver(&observer);
}

}  // namespace update_client
