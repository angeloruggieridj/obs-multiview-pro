// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "appearance.hpp"

#include <QDialog>
#include <QPointer>
#include <QTimer>

class QCheckBox;
class QColorDialog;
class QEvent;
class QKeyEvent;
class QShowEvent;
class QLabel;
class QLineEdit;
class QTabWidget;
class QTextBrowser;
class QPushButton;
class QSlider;
class QSpinBox;
class QDialogButtonBox;

/* Pages of the settings dialog. */
enum class SettingsPage { Keep, Appearance, Updates, About };

/* The plugin settings: an "Appearance" tab (frame colour under the tiles,
 * label opacity, meter opacity, meters on the scene tiles, windows always on
 * top), an "Updates" tab (installed version, check
 * now, automatic check and pre-release options, change log of the newer
 * releases, download page, skip) and an "About" tab.
 *
 * Appearance changes preview live in every open multiview window
 * (SettingsStore::preview_appearance, nothing is saved). Apply saves them and
 * the two update options, OK saves and closes, Cancel (or closing the dialog)
 * takes the preview back and drops unsaved option changes. "Check for updates
 * now" and "Skip this version" act at once.
 *
 * The preview is sent 50 ms after the last change (trailing edge).
 *
 * While the multiview windows are always on top (saved or previewed), the
 * dialog and its colour chooser are kept above them too, so they never open
 * hidden behind a topmost window (Windows: the topmost band of the native
 * window; elsewhere Qt::WindowStaysOnTopHint).
 *
 * UI thread only. Not modal, so the windows can be watched while editing. */
class SettingsDialog : public QDialog {
	Q_OBJECT

public:
	explicit SettingsDialog(QWidget *parent = nullptr);

	void show_page(SettingsPage page);

	/* Shows the saved values again (the dialog is reused while it is open). */
	void reload();

	void reject() override;

	/* A multiview window changed its topmost band (a delayed retry): come
	 * back above it while the windows are on top. */
	void reassert_on_top();

protected:
	void keyPressEvent(QKeyEvent *event) override;
	void showEvent(QShowEvent *event) override;
	void changeEvent(QEvent *event) override;

private:
	QWidget *build_appearance_page();
	QWidget *build_updates_page();
	QWidget *build_about_page();
	/* Shows UpdateManager's state in the Updates tab. */
	void show_update_state();
	/* Puts `a` in the controls (the hex field only when `rewrite_hex`, so a
	 * half-typed value is not replaced); nothing is previewed. */
	void show_values(const Appearance &a, bool rewrite_hex = true);
	/* The user changed a value: show it and preview it in the windows. */
	void values_edited(const Appearance &a, bool rewrite_hex = true);
	/* A toggle a window menu can also set was saved (see follow_saved_toggles). */
	void follow_saved(const Appearance &saved);
	/* Keeps the dialog (and the colour chooser) above topmost windows. */
	void set_on_top(bool on);
	void apply_on_top();
	void check_on_top();
	void pick_color();
	void apply();
	void update_buttons();

	Appearance m_saved;   /* what the store holds */
	Appearance m_current; /* what the controls show */
	bool m_saved_auto_check = true;
	bool m_saved_prereleases = false;
	bool m_updating = false;
	/* Coalesces the preview: every window re-rasterizes its labels for a new
	 * opacity, so a slider drag must not trigger one per step. */
	QTimer m_preview_timer;

	/* The open colour chooser (a child of this dialog, non-modal-loop: deleted
	 * with the dialog, never on the stack). */
	QPointer<QColorDialog> m_color_dialog;
	QPushButton *m_swatch = nullptr;
	QLineEdit *m_hex = nullptr;
	QSlider *m_label_slider = nullptr;
	QSpinBox *m_label_spin = nullptr;
	QSlider *m_meter_slider = nullptr;
	QSpinBox *m_meter_spin = nullptr;
	QCheckBox *m_on_top = nullptr;
	QCheckBox *m_tile_meters = nullptr;
	bool m_keep_on_top = false; /* the windows are on top now (saved or previewed) */
	/* Windows: a change of the topmost band made while a window's own popup
	 * menu closes may not stick; look again a little later, a few times. */
	QTimer m_on_top_timer;
	int m_on_top_checks = 0;
	bool m_on_top_warned = false; /* logged once: the band would not stick */
	QDialogButtonBox *m_buttons = nullptr;

	QTabWidget *m_tabs = nullptr;
	QWidget *m_updates_page = nullptr;
	QLabel *m_update_status = nullptr;
	QPushButton *m_check_now = nullptr;
	QCheckBox *m_auto_check = nullptr;
	QCheckBox *m_prereleases = nullptr;
	QTextBrowser *m_changelog = nullptr;
	QPushButton *m_open_page = nullptr;
	QPushButton *m_skip = nullptr;
};

/* Tools menu / context menu entry point: opens the dialog, or raises the one
 * that is open (at most one), on `page`. UI thread. */
void open_settings_dialog(SettingsPage page = SettingsPage::Keep);
/* Closes and deletes the dialog if it is open, taking back any preview. Call
 * before the windows and the store go away (OBS exit, plugin unload). */
void close_settings_dialog();
/* A multiview window's topmost band changed after a delay: the open dialog,
 * if any, re-asserts its own above it. UI thread. */
void settings_dialog_reassert_on_top();
