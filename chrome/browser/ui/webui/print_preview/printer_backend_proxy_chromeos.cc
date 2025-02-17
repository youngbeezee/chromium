// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/print_preview/printer_backend_proxy.h"

#include <memory>
#include <vector>

#include "base/bind_helpers.h"
#include "base/command_line.h"
#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/task_scheduler/post_task.h"
#include "base/values.h"
#include "chrome/browser/chromeos/printing/cups_print_job_manager.h"
#include "chrome/browser/chromeos/printing/cups_print_job_manager_factory.h"
#include "chrome/browser/chromeos/printing/ppd_provider_factory.h"
#include "chrome/browser/chromeos/printing/printer_configurer.h"
#include "chrome/browser/chromeos/printing/printers_manager.h"
#include "chrome/browser/chromeos/printing/printers_manager_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/webui/print_preview/printer_capabilities.h"
#include "chrome/common/chrome_switches.h"
#include "chromeos/dbus/dbus_thread_manager.h"
#include "chromeos/dbus/debug_daemon_client.h"
#include "chromeos/printing/ppd_provider.h"
#include "chromeos/printing/printer_configuration.h"
#include "content/public/browser/browser_thread.h"
#include "printing/backend/print_backend_consts.h"

namespace printing {

namespace {

// Store the name used in CUPS, Printer#id in |printer_name|, the description
// as the system_driverinfo option value, and the Printer#display_name in
// the |printer_description| field.  This will match how Mac OS X presents
// printer information.
printing::PrinterBasicInfo ToBasicInfo(const chromeos::Printer& printer) {
  PrinterBasicInfo basic_info;

  // TODO(skau): Unify Mac with the other platforms for display name
  // presentation so I can remove this strange code.
  basic_info.options[kDriverInfoTagName] = printer.description();
  basic_info.options[kCUPSEnterprisePrinter] =
      (printer.source() == chromeos::Printer::SRC_POLICY) ? kValueTrue
                                                          : kValueFalse;
  basic_info.printer_name = printer.id();
  basic_info.printer_description = printer.display_name();
  return basic_info;
}

void AddPrintersToList(
    const std::vector<std::unique_ptr<chromeos::Printer>>& printers,
    PrinterList* list) {
  for (const auto& printer : printers) {
    list->push_back(ToBasicInfo(*printer));
  }
}

void FetchCapabilities(std::unique_ptr<chromeos::Printer> printer,
                       const PrinterSetupCallback& cb) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  PrinterBasicInfo basic_info = ToBasicInfo(*printer);

  base::PostTaskWithTraitsAndReplyWithResult(
      FROM_HERE,
      base::TaskTraits().MayBlock().WithPriority(
          base::TaskPriority::BACKGROUND),
      base::Bind(&GetSettingsOnBlockingPool, printer->id(), basic_info), cb);
}

void HandlePrinterSetup(std::unique_ptr<chromeos::Printer> printer,
                        const PrinterSetupCallback& cb,
                        chromeos::PrinterSetupResult result) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

  switch (result) {
    case chromeos::PrinterSetupResult::SUCCESS:
      VLOG(1) << "Printer setup successful for " << printer->id()
              << " fetching properties";

      // fetch settings on the blocking pool and invoke callback.
      FetchCapabilities(std::move(printer), cb);
      return;
    case chromeos::PrinterSetupResult::PPD_NOT_FOUND:
      LOG(WARNING) << "Could not find PPD.  Check printer configuration.";
      // Prompt user to update configuration.
      // TODO(skau): Fill me in
      break;
    case chromeos::PrinterSetupResult::PPD_UNRETRIEVABLE:
      LOG(WARNING) << "Could not download PPD.  Check Internet connection.";
      // Could not download PPD.  Connect to Internet.
      // TODO(skau): Fill me in
      break;
    case chromeos::PrinterSetupResult::PRINTER_UNREACHABLE:
    case chromeos::PrinterSetupResult::DBUS_ERROR:
    case chromeos::PrinterSetupResult::PPD_TOO_LARGE:
    case chromeos::PrinterSetupResult::INVALID_PPD:
    case chromeos::PrinterSetupResult::FATAL_ERROR:
      LOG(ERROR) << "Unexpected error in printer setup." << result;
      break;
  }

  // TODO(skau): Open printer settings if this is resolvable.
  cb.Run(nullptr);
}

class PrinterBackendProxyChromeos : public PrinterBackendProxy {
 public:
  explicit PrinterBackendProxyChromeos(Profile* profile)
      : prefs_(chromeos::PrintersManagerFactory::GetForBrowserContext(profile)),
        printer_configurer_(chromeos::PrinterConfigurer::Create(profile)) {
    // Construct the CupsPrintJobManager to listen for printing events.
    chromeos::CupsPrintJobManagerFactory::GetForBrowserContext(profile);
  }

  ~PrinterBackendProxyChromeos() override {
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  }

  void GetDefaultPrinter(const DefaultPrinterCallback& cb) override {
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
    // TODO(crbug.com/660898): Add default printers to ChromeOS.

    content::BrowserThread::PostTask(content::BrowserThread::UI, FROM_HERE,
                                     base::Bind(cb, ""));
  };

  void EnumeratePrinters(const EnumeratePrintersCallback& cb) override {
    // PrintersManager is not thread safe and must be called from the UI
    // thread.
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

    PrinterList printer_list;

    if (!base::CommandLine::ForCurrentProcess()->HasSwitch(
            switches::kDisableNativeCups)) {
      AddPrintersToList(prefs_->GetPrinters(), &printer_list);
      AddPrintersToList(prefs_->GetRecommendedPrinters(), &printer_list);
    }

    content::BrowserThread::PostTask(content::BrowserThread::UI, FROM_HERE,
                                     base::Bind(cb, printer_list));
  };

  void ConfigurePrinterAndFetchCapabilities(
      const std::string& printer_name,
      const PrinterSetupCallback& cb) override {
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);

    std::unique_ptr<chromeos::Printer> printer =
        prefs_->GetPrinter(printer_name);
    if (!printer) {
      // If the printer was removed, the lookup will fail.
      content::BrowserThread::PostTask(content::BrowserThread::UI, FROM_HERE,
                                       base::Bind(cb, nullptr));
      return;
    }

    const chromeos::Printer& printer_ref = *printer;
    printer_configurer_->SetUpPrinter(
        printer_ref,
        base::Bind(&HandlePrinterSetup, base::Passed(&printer), cb));
  };

 private:
  chromeos::PrintersManager* prefs_;
  scoped_refptr<chromeos::printing::PpdProvider> ppd_provider_;
  std::unique_ptr<chromeos::PrinterConfigurer> printer_configurer_;

  DISALLOW_COPY_AND_ASSIGN(PrinterBackendProxyChromeos);
};

}  // namespace

std::unique_ptr<PrinterBackendProxy> PrinterBackendProxy::Create(
    Profile* profile) {
  return base::MakeUnique<PrinterBackendProxyChromeos>(profile);
}

}  // namespace printing
