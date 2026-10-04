// gapless_fix.h
#ifndef GAPLESS_FIX_H
#define GAPLESS_FIX_H

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <mmsystem.h>

#include <commctrl.h>
#include <atlbase.h>
#include <atlapp.h>
extern CAppModule _Module;
#include <atlwin.h>
#include <atlctrls.h>
#include <atlcrack.h>

#include "foobar2000.h"
#include "resource.h"

// ============================================================================
// Advanced configuration
// ============================================================================
static const GUID guid_advconfig_branch =
{ 0xa1b2c3d4, 0xe5f6, 0x7a8b, { 0x9c, 0x0d, 0x1e, 0x2f, 0x3a, 0x4b, 0x5c, 0x6d } };

static const GUID guid_advconfig_subfolder =
{ 0xb2c3d4e5, 0xf6a7, 0x8b9c, { 0x0d, 0x1e, 0x2f, 0x3a, 0x4b, 0x5c, 0x6d, 0x7e } };

static const GUID guid_advconfig_subfolder_name =
{ 0xc3d4e5f6, 0xa7b8, 0x9c0d, { 0x1e, 0x2f, 0x3a, 0x4b, 0x5c, 0x6d, 0x7e, 0x8f } };

static const GUID guid_gapless_fix_item =
{ 0x7c3d5e6f, 0x8a9b, 0x0c1d, { 0x2e, 0x3f, 0x4a, 0x5b, 0x6c, 0x7d, 0x8e, 0x9f } };

// ============================================================================
// Result of analysis for one track
// ============================================================================
struct TrackAnalysisResult {
    metadb_handle_ptr track;
    bool needs_fix;
    t_size samples_to_add;
    t_size total_samples;
    unsigned bps;
    unsigned srate;
};

// ============================================================================
// Analysis threaded_process callback
// ============================================================================
class AnalysisProcess : public threaded_process_callback {
public:
    AnalysisProcess(metadb_handle_list_cref tracks);

    void run(threaded_process_status& p_status, abort_callback& p_abort) override;
    void on_done(HWND p_wnd, bool p_was_aborted) override;

    metadb_handle_list m_tracks;
    pfc::list_t<TrackAnalysisResult> m_results;
    bool m_success = false;
};

// ============================================================================
// Playlist add notify callback
// ============================================================================
class PlaylistAddNotify : public process_locations_notify {
public:
    void on_completion(const pfc::list_base_const_t<metadb_handle_ptr>& p_items) override;
    void on_aborted() override;
};

// ============================================================================
// Fix threaded_process callback
// ============================================================================
class FixProcess : public threaded_process_callback {
public:
    FixProcess(metadb_handle_list_cref tracks,
        pfc::list_t<TrackAnalysisResult> const& results,
        double extra_seconds,
        bool create_subfolder,
        const char* subfolder_name);

    void run(threaded_process_status& p_status, abort_callback& p_abort) override;
    void on_done(HWND p_wnd, bool p_was_aborted) override;

private:
    metadb_handle_list m_tracks;
    pfc::list_t<TrackAnalysisResult> m_results;
    double m_extra_seconds;
    bool m_create_subfolder;
    pfc::string8 m_subfolder_name;
    bool m_failed = false;
    pfc::list_t<pfc::string8> m_output_paths;
    bool GetOutputPath(const char* input_path, pfc::string8& out_path);
};

// ============================================================================
// Modeless dialog class
// ============================================================================
class CGaplessFixDialog : public CDialogImpl<CGaplessFixDialog> {
public:
    enum { IDD = IDD_GAPLESS_FIX_DIALOG };

    metadb_handle_list m_tracks;
    pfc::list_t<TrackAnalysisResult> m_results;

    BEGIN_MSG_MAP(CGaplessFixDialog)
        MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
        MESSAGE_HANDLER(WM_CLOSE, OnClose)
        MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
        COMMAND_ID_HANDLER(IDC_BTN_FIX, OnFix)
        COMMAND_ID_HANDLER(IDC_BTN_EXIT, OnExit)
    END_MSG_MAP()

    LRESULT OnInitDialog(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnClose(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnDestroy(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnFix(WORD, WORD, HWND, BOOL&);
    LRESULT OnExit(WORD, WORD, HWND, BOOL&);

    void OnFinalMessage(HWND);

private:
    CListViewCtrl m_list;
    CEdit m_edit_seconds;
    void PopulateList();
};

// ============================================================================
// Context menu item
// ============================================================================
class GaplessFixMenu : public contextmenu_item_simple {
public:
    enum { cmd_gapless_fix = 0 };

    virtual unsigned get_num_items() { return 1; }

    virtual void get_item_name(unsigned p_index, pfc::string_base& p_out) {
        if (p_index == cmd_gapless_fix) p_out = "Gapless Fix";
    }

    virtual void get_item_default_path(unsigned p_index, pfc::string_base& p_out) {
        if (p_index == cmd_gapless_fix) p_out = "Utilities";
    }

    virtual bool get_item_description(unsigned p_index, pfc::string_base& p_out) {
        if (p_index == cmd_gapless_fix) {
            p_out = "Adds silence to complete CD frames (588 samples).";
            return true;
        }
        return false;
    }

    virtual GUID get_item_guid(unsigned p_index) {
        if (p_index == cmd_gapless_fix) return guid_gapless_fix_item;
        return pfc::guid_null;
    }

    virtual void context_command(unsigned p_index, metadb_handle_list_cref p_data, const GUID& p_caller);
};

static contextmenu_item_factory_t<GaplessFixMenu> g_gapless_fix_menu_factory;

#endif // GAPLESS_FIX_H