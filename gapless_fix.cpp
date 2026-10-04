// gapless_fix.cpp
#include "gapless_fix.h"
#include <helpers/input_helpers.h>
#include <helpers/writer_wav.h>

// ============================================================================
// Advanced configuration factories
// ============================================================================
static advconfig_branch_factory g_advconfig_branch_factory(
    "Gapless Fix",
    guid_advconfig_branch,
    advconfig_branch::guid_branch_tools,
    0
);

static advconfig_checkbox_factory g_advconfig_subfolder_factory(
    "Create subfolder for output",
    guid_advconfig_subfolder,
    guid_advconfig_branch,
    0,
    false
);

static advconfig_string_factory g_advconfig_subfolder_name_factory(
    "Subfolder name",
    guid_advconfig_subfolder_name,
    guid_advconfig_branch,
    0,
    "gap_fixed"
);

// ============================================================================
// PlaylistAddNotify
// ============================================================================
void PlaylistAddNotify::on_completion(const pfc::list_base_const_t<metadb_handle_ptr>& p_items) {
    if (p_items.get_count() == 0) return;
    static_api_ptr_t<playlist_manager>()->activeplaylist_insert_items(
        pfc::infinite_size, p_items, bit_array_true()
    );
}

void PlaylistAddNotify::on_aborted() {
}

// ============================================================================
// Context menu handler
// ============================================================================
void GaplessFixMenu::context_command(unsigned p_index, metadb_handle_list_cref p_data, const GUID& p_caller) {
    if (p_index != cmd_gapless_fix) return;
    if (p_data.get_count() == 0) return;

    service_ptr_t<AnalysisProcess> proc = new service_impl_t<AnalysisProcess>(p_data);
    threaded_process::g_run_modeless(
        proc,
        threaded_process::flag_show_progress | threaded_process::flag_show_abort,
        core_api::get_main_window(),
        "Gapless Fix: Analyzing tracks..."
    );
}

// ============================================================================
// AnalysisProcess
// ============================================================================
AnalysisProcess::AnalysisProcess(metadb_handle_list_cref tracks) : m_tracks(tracks) {}

void AnalysisProcess::run(threaded_process_status& p_status, abort_callback& p_abort) {
    p_status.set_title("Analyzing...");
    t_size total = m_tracks.get_count();
    m_results.remove_all();

    for (t_size i = 0; i < total; i++) {
        p_abort.check();
        p_status.set_progress(i, total);
        p_status.set_item_path(m_tracks[i]->get_path());

        try {
            TrackAnalysisResult r;
            r.track = m_tracks[i];
            r.bps = 0;
            r.srate = 0;
            r.total_samples = 0;

            // --- Быстрый путь: получаем данные из метаданных ---
            file_info_impl info;
            if (m_tracks[i]->get_info(info)) {
                const file_info& base = info;
                r.bps = (unsigned)base.info_get_int("bitspersample");
                r.srate = (unsigned)base.info_get_int("samplerate");

                t_int64 len_samples = base.info_get_length_samples();
                if (len_samples > 0) {
                    r.total_samples = (t_size)len_samples;
                }
            }

            // --- Медленный путь: если метаданные не дали точную длину, декодируем ---
            if (r.total_samples == 0) {
                input_helper helper;
                helper.open(nullptr, m_tracks[i], input_flag_no_looping, p_abort, false, false);

                audio_chunk_impl chunk;
                bool first = true;

                while (helper.run(chunk, p_abort)) {
                    if (first) {
                        r.srate = chunk.get_srate();
                        if (r.bps == 0) {
                            file_info_impl info2;
                            if (m_tracks[i]->get_info(info2)) {
                                const file_info& base2 = info2;
                                r.bps = (unsigned)base2.info_get_int("bitspersample");
                            }
                        }
                        first = false;
                    }
                    r.total_samples += chunk.get_sample_count();
                }
                helper.close();
            }

            // --- Классификация ---
            if (r.bps != 16 || r.srate != 44100) {
                r.needs_fix = false;
                r.samples_to_add = 0;
            }
            else {
                t_size rem = r.total_samples % 588;
                r.needs_fix = (rem != 0);
                r.samples_to_add = r.needs_fix ? (588 - rem) : 0;
            }

            m_results.add_item(r);
        }
        catch (const std::exception& e) {
            FB2K_console_formatter() << "Gapless Fix: analysis error on "
                << m_tracks[i]->get_path() << " - " << e.what();
            TrackAnalysisResult r;
            r.track = m_tracks[i];
            r.needs_fix = false;
            r.samples_to_add = 0;
            r.total_samples = 0;
            r.bps = 0;
            r.srate = 0;
            m_results.add_item(r);
        }
        catch (...) {
            FB2K_console_formatter() << "Gapless Fix: unknown analysis error on "
                << m_tracks[i]->get_path();
            TrackAnalysisResult r;
            r.track = m_tracks[i];
            r.needs_fix = false;
            r.samples_to_add = 0;
            r.total_samples = 0;
            r.bps = 0;
            r.srate = 0;
            m_results.add_item(r);
        }
    }
    m_success = true;
}

void AnalysisProcess::on_done(HWND p_wnd, bool p_was_aborted) {
    if (p_was_aborted || !m_success) return;

    FB2K_console_formatter() << "Gapless Fix: on_done, results = " << m_results.get_count();

    CGaplessFixDialog* dlg = new CGaplessFixDialog();
    dlg->m_tracks = m_tracks;
    dlg->m_results = m_results;

    HWND hwnd = dlg->Create(core_api::get_main_window());
    FB2K_console_formatter() << "Gapless Fix: dialog hwnd = " << pfc::format_uint((t_size)hwnd);

    if (hwnd == NULL) {
        FB2K_console_formatter() << "Gapless Fix: dialog creation FAILED";
        delete dlg;
    }
    else {
        dlg->ShowWindow(SW_SHOW);
        dlg->SetWindowPos(HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    }
}

// ============================================================================
// FixProcess
// ============================================================================
FixProcess::FixProcess(metadb_handle_list_cref tracks,
    pfc::list_t<TrackAnalysisResult> const& results,
    double extra_seconds,
    bool create_subfolder,
    const char* subfolder_name)
    : m_tracks(tracks),
    m_results(results),
    m_extra_seconds(extra_seconds),
    m_create_subfolder(create_subfolder),
    m_subfolder_name(subfolder_name) {
}

bool FixProcess::GetOutputPath(const char* input_path, pfc::string8& out_path) {
    pfc::string8 dir, filename;
    pfc::string8 full_path = input_path;

    t_size pos = full_path.find_last('\\');
    if (pos == pfc::infinite_size) return false;

    dir = full_path;
    dir.truncate(pos);
    filename = full_path.get_ptr() + pos + 1;

    t_size dot = filename.find_last('.');
    if (dot != pfc::infinite_size) filename.truncate(dot);

    if (m_create_subfolder) {
        out_path = dir;
        out_path << "\\" << m_subfolder_name;

        try {
            abort_callback_dummy aborter;
            filesystem::g_create_directory(out_path, aborter);
        }
        catch (const exception_io_already_exists&) {
        }

        out_path << "\\" << filename << "_gap_fixed.wav";
    }
    else {
        out_path = dir;
        out_path << "\\" << filename << "_gap_fixed.wav";
    }
    return true;
}

void FixProcess::run(threaded_process_status& p_status, abort_callback& p_abort) {
    p_status.set_title("Writing fixed WAV files...");
    t_size total = m_results.get_count();
    t_size current = 0;

    for (t_size i = 0; i < total; i++) {
        p_abort.check();
        TrackAnalysisResult& r = m_results[i];

        if (r.bps != 16 || r.srate != 44100) continue;
        if (!r.needs_fix && m_extra_seconds == 0.0) continue;

        current++;
        p_status.set_progress(current, total);
        p_status.set_item_path(r.track->get_path());

        pfc::string8 input_path = r.track->get_path();
        pfc::string8 output_path;
        if (!GetOutputPath(input_path, output_path)) continue;

        try {
            input_helper helper;
            helper.open(nullptr, r.track, input_flag_no_looping, p_abort, false, false);

            audio_chunk_impl first_chunk;
            if (!helper.run(first_chunk, p_abort)) continue;

            unsigned srate = first_chunk.get_srate();

            wavWriterSetup_t setup;
            setup.initialize(first_chunk, 16, false, false);

            CWavWriter writer;
            writer.open(output_path, setup, p_abort);

            t_size samples_to_crop = 0;
            if (m_extra_seconds < 0.0) {
                samples_to_crop = (t_size)((-m_extra_seconds) * srate);
            }

            t_size total_track_samples = r.total_samples;
            t_size samples_written = 0;

            t_size write_limit = total_track_samples;
            if (samples_to_crop > 0 && total_track_samples > samples_to_crop) {
                write_limit = total_track_samples - samples_to_crop;
            }
            else if (samples_to_crop > 0) {
                write_limit = 0;
            }

            {
                t_size chunk_samples = first_chunk.get_sample_count();
                t_size write_count = chunk_samples;

                if (write_limit < total_track_samples) {
                    if (samples_written >= write_limit) {
                        write_count = 0;
                    }
                    else if (samples_written + chunk_samples > write_limit) {
                        write_count = write_limit - samples_written;
                    }
                }

                if (write_count > 0) {
                    if (write_count < chunk_samples) {
                        first_chunk.set_sample_count(write_count);
                    }
                    writer.write(first_chunk, p_abort);
                    samples_written += write_count;
                }
            }

            audio_chunk_impl chunk;
            while (helper.run(chunk, p_abort)) {
                t_size chunk_samples = chunk.get_sample_count();
                t_size write_count = chunk_samples;

                if (write_limit < total_track_samples) {
                    if (samples_written >= write_limit) {
                        write_count = 0;
                    }
                    else if (samples_written + chunk_samples > write_limit) {
                        write_count = write_limit - samples_written;
                    }
                }

                if (write_count > 0) {
                    if (write_count < chunk_samples) {
                        chunk.set_sample_count(write_count);
                    }
                    writer.write(chunk, p_abort);
                    samples_written += write_count;
                }
            }
            helper.close();

            if (r.samples_to_add > 0) {
                audio_chunk_impl silence_chunk;
                silence_chunk.set_srate(srate);
                silence_chunk.set_channels(first_chunk.get_channels(),
                    first_chunk.get_channel_config());
                silence_chunk.set_silence(r.samples_to_add);
                writer.write(silence_chunk, p_abort);
            }

            if (m_extra_seconds > 0.0) {
                t_size extra_samples = (t_size)(m_extra_seconds * srate);
                if (extra_samples > 0) {
                    audio_chunk_impl silence_chunk;
                    silence_chunk.set_srate(srate);
                    silence_chunk.set_channels(first_chunk.get_channels(),
                        first_chunk.get_channel_config());
                    silence_chunk.set_silence(extra_samples);
                    writer.write(silence_chunk, p_abort);
                }
            }

            writer.finalize(p_abort);
            writer.close();

            m_output_paths.add_item(output_path);

            // --- Теги ---
            {
                file_info_impl info;
                if (r.track->get_info(info)) {
                    replaygain_info rg = info.get_replaygain();
                    rg.reset();
                    info.set_replaygain(rg);

                    info.meta_remove_field("LENGTH");
                    info.meta_remove_field("TLEN");

                    try {
                        service_ptr_t<input_info_writer> p_writer;
                        input_entry::g_open_for_info_write(p_writer, nullptr, output_path.get_ptr(), p_abort);
                        p_writer->set_info(0, info, p_abort);
                        p_writer->commit(p_abort);
                    }
                    catch (const std::exception& e) {
                        FB2K_console_formatter() << "Gapless Fix: failed to write tags - " << e.what();
                        m_failed = true;
                    }
                }
            }

            // --- Обложки ---
            {
                pfc::string8 src_path = r.track->get_path();
                try {
                    album_art_extractor_instance_ptr extractor =
                        album_art_extractor::g_open(nullptr, src_path.get_ptr(), p_abort);
                    album_art_editor_instance_ptr editor =
                        album_art_editor::g_open(nullptr, output_path.get_ptr(), p_abort);

                    static const GUID* const art_types[] = {
                        &album_art_ids::cover_front,
                        &album_art_ids::cover_back,
                        &album_art_ids::disc,
                        &album_art_ids::icon,
                        &album_art_ids::artist
                    };

                    bool modified = false;
                    for (const GUID* type_guid : art_types) {
                        try {
                            album_art_data_ptr data = extractor->query(*type_guid, p_abort);
                            if (data.is_valid()) {
                                editor->set(*type_guid, data, p_abort);
                                modified = true;
                            }
                        }
                        catch (const exception_album_art_not_found&) {}
                        catch (...) {}
                    }

                    if (modified) {
                        editor->commit(p_abort);
                    }
                }
                catch (const exception_album_art_not_found&) {}
                catch (const exception_album_art_unsupported_format&) {}
                catch (const std::exception& e) {
                    FB2K_console_formatter() << "Gapless Fix: failed to copy album art - " << e.what();
                    m_failed = true;
                }
            }

            FB2K_console_formatter() << "Gapless Fix: wrote " << output_path;
        }
        catch (const exception_aborted&) {
            // Пользователь отменил — удаляем недо-записанный файл
            try {
                abort_callback_dummy aborter;
                filesystem::g_remove(output_path, aborter);
                FB2K_console_formatter() << "Gapless Fix: removed incomplete file " << output_path;
            }
            catch (...) {}
            throw;  // пробрасываем дальше, чтобы threaded_process корректно завершился
        }
        catch (const std::exception& e) {
            FB2K_console_formatter() << "Gapless Fix: error on " << input_path << " - " << e.what();
            m_failed = true;
        }
    }
}

void FixProcess::on_done(HWND p_wnd, bool p_was_aborted) {
    if (p_was_aborted) {
        FB2K_console_formatter() << "Gapless Fix: operation aborted by user";
        return;
    }
    if (m_failed) {
        popup_message::g_show("Gapless Fix completed with errors. See Console for details.",
            "Gapless Fix", popup_message::icon_error);
        return;
    }

    FB2K_console_formatter() << "Gapless Fix: all files written successfully";

    if (m_output_paths.get_count() > 0) {
        pfc::list_t<const char*> paths;
        for (t_size i = 0; i < m_output_paths.get_count(); i++) {
            paths.add_item(m_output_paths[i].get_ptr());
        }

        service_ptr_t<PlaylistAddNotify> notify = new service_impl_t<PlaylistAddNotify>();
        static_api_ptr_t<playlist_incoming_item_filter_v2>()->process_locations_async(
            paths,
            playlist_incoming_item_filter_v2::op_flag_background,
            NULL,
            NULL,
            core_api::get_main_window(),
            notify
        );
    }
}

// ============================================================================
// Dialog
// ============================================================================
LRESULT CGaplessFixDialog::OnInitDialog(UINT, WPARAM, LPARAM, BOOL&) {
    modeless_dialog_manager::g_add(m_hWnd);

    m_list.Attach(GetDlgItem(IDC_LIST_TRACKS));
    m_list.InsertColumn(0, L"Track", LVCFMT_LEFT, 250);
    m_list.InsertColumn(1, L"Status", LVCFMT_LEFT, 120);
    m_list.InsertColumn(2, L"Missing samples", LVCFMT_RIGHT, 120);

    m_edit_seconds.Attach(GetDlgItem(IDC_EDIT_SECONDS));
    m_edit_seconds.SetWindowText(L"0");

    PopulateList();
    return TRUE;
}

LRESULT CGaplessFixDialog::OnClose(UINT, WPARAM, LPARAM, BOOL&) {
    DestroyWindow();
    return 0;
}

LRESULT CGaplessFixDialog::OnDestroy(UINT, WPARAM, LPARAM, BOOL&) {
    modeless_dialog_manager::g_remove(m_hWnd);
    return 0;
}

void CGaplessFixDialog::OnFinalMessage(HWND) {
    delete this;
}

void CGaplessFixDialog::PopulateList() {
    m_list.DeleteAllItems();

    for (t_size i = 0; i < m_results.get_count(); i++) {
        const TrackAnalysisResult& r = m_results[i];

        pfc::string8 name = pfc::string_filename_ext(r.track->get_path());
        pfc::stringcvt::string_wide_from_utf8 wide_name(name);
        int idx = m_list.InsertItem(i, wide_name);

        if (r.bps != 16 || r.srate != 44100) {
            m_list.SetItemText(idx, 1, L"Unsupported");
            m_list.SetItemText(idx, 2, L"-");
        }
        else if (r.needs_fix) {
            m_list.SetItemText(idx, 1, L"Gap");
            pfc::string8 samples_str;
            samples_str << pfc::format_uint(r.samples_to_add);
            pfc::stringcvt::string_wide_from_utf8 wide_samples(samples_str);
            m_list.SetItemText(idx, 2, wide_samples);
        }
        else {
            m_list.SetItemText(idx, 1, L"OK");
            m_list.SetItemText(idx, 2, L"-");
        }
    }
}

LRESULT CGaplessFixDialog::OnFix(WORD, WORD, HWND, BOOL&) {
    wchar_t sec_buf[32];
    m_edit_seconds.GetWindowText(sec_buf, 32);

    pfc::stringcvt::string_utf8_from_os sec_utf8(sec_buf);
    double extra_seconds = pfc::string_to_float(sec_utf8);

    bool create_subfolder = g_advconfig_subfolder_factory.get();
    pfc::string8 subfolder_name;
    g_advconfig_subfolder_name_factory.get(subfolder_name);

    metadb_handle_list tracks_copy = m_tracks;
    pfc::list_t<TrackAnalysisResult> results_copy = m_results;

    DestroyWindow();

    service_ptr_t<FixProcess> proc = new service_impl_t<FixProcess>(
        tracks_copy, results_copy, extra_seconds,
        create_subfolder, subfolder_name.get_ptr());
    threaded_process::g_run_modeless(
        proc,
        threaded_process::flag_show_progress | threaded_process::flag_show_abort,
        core_api::get_main_window(),
        "Gapless Fix: Writing files..."
    );

    return 0;
}

LRESULT CGaplessFixDialog::OnExit(WORD, WORD, HWND, BOOL&) {
    DestroyWindow();
    return 0;
}