// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "appearance.hpp"

#include <QDialog>

class QLineEdit;
class QPushButton;
class QSlider;
class QSpinBox;
class QDialogButtonBox;

/* The plugin settings: an "Appearance" tab (frame colour under the tiles,
 * label opacity, meter opacity) and an "About" tab.
 *
 * Changes preview live in every open multiview window (SettingsStore::
 * preview_appearance, nothing is saved). Apply saves them, OK saves and
 * closes, Cancel (or closing the dialog) takes the preview back.
 *
 * UI thread only. Not modal, so the windows can be watched while editing. */
class SettingsDialog : public QDialog {
	Q_OBJECT

public:
	explicit SettingsDialog(QWidget *parent = nullptr);

	/* Shows the saved values again (the dialog is reused while it is open). */
	void reload();

	void reject() override;

private:
	QWidget *build_appearance_page();
	QWidget *build_about_page();
	/* Puts `a` in the controls (the hex field only when `rewrite_hex`, so a
	 * half-typed value is not replaced); nothing is previewed. */
	void show_values(const Appearance &a, bool rewrite_hex = true);
	/* The user changed a value: show it and preview it in the windows. */
	void values_edited(const Appearance &a, bool rewrite_hex = true);
	void pick_color();
	void apply();
	void update_buttons();

	Appearance m_saved;   /* what the store holds */
	Appearance m_current; /* what the controls show */
	bool m_updating = false;

	QPushButton *m_swatch = nullptr;
	QLineEdit *m_hex = nullptr;
	QSlider *m_label_slider = nullptr;
	QSpinBox *m_label_spin = nullptr;
	QSlider *m_meter_slider = nullptr;
	QSpinBox *m_meter_spin = nullptr;
	QDialogButtonBox *m_buttons = nullptr;
};

/* Tools menu / context menu entry point: opens the dialog, or raises the one
 * that is open (at most one). UI thread. */
void open_settings_dialog();
/* Closes and deletes the dialog if it is open, taking back any preview. Call
 * before the windows and the store go away (OBS exit, plugin unload). */
void close_settings_dialog();
