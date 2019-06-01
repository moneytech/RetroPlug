#include "EmulatorView.h"

#include "platform/FileDialog.h"
#include "platform/Path.h"
#include "util/File.h"
#include "util/Serializer.h"

#include <tao/json.hpp>

EmulatorView::EmulatorView(IRECT bounds, SameBoyPlugPtr plug, RetroPlug* manager) 
	: IControl(bounds), _plug(plug), _manager(manager)
{
	memset(_videoScratch, 255, VIDEO_SCRATCH_SIZE);

	_settings = {
		{ "Color Correction", 2 },
		{ "High-pass Filter", 1 }
	};

	tao::json::value config;
	loadButtonConfig(config);
	_keyMap.load(config.at("gameboy"));
	_lsdjKeyMap.load(_keyMap, config.at("lsdj"));
}

EmulatorView::~EmulatorView() {
	ShowText(false);

	if (_imageId != -1 && GetUI()) {
		NVGcontext* ctx = (NVGcontext*)GetUI()->GetDrawContext();
		nvgDeleteImage(ctx, _imageId);
	}
}

void EmulatorView::Setup(SameBoyPlugPtr plug, RetroPlug * manager) {
	_plug = plug;
	_manager = manager;
	ShowText(false);
}

void EmulatorView::OnInit() {
	
}

void EmulatorView::OnDrop(const char* str) {
	ShowText(false);
	_plug->init(str, GameboyModel::Auto);
}

bool EmulatorView::OnKey(const IKeyPress& key, bool down) {
	if (_plug && _plug->active()) {
		if (_plug->lsdj().found && _plug->lsdj().keyboardShortcuts) {
			return _lsdjKeyMap.onKey(key, down);
		} else {
			ButtonEvent ev;
			ev.id = _keyMap.getControllerButton((VirtualKey)key.VK);
			ev.down = down;

			if (ev.id != ButtonTypes::MAX) {
				_plug->setButtonState(ev);
				return true;
			}
		}
	}

	return false;
}

void EmulatorView::OnMouseDblClick(float x, float y, const IMouseMod& mod) {
	OpenLoadRomDialog(GameboyModel::Auto);
}

void EmulatorView::OnMouseDown(float x, float y, const IMouseMod& mod) {
	if (mod.R) {
		CreateMenu(x, y);
	}
}

void EmulatorView::OnPopupMenuSelection(IPopupMenu* selectedMenu, int valIdx) {
	if (selectedMenu) {
		if (selectedMenu->GetFunction()) {
			selectedMenu->ExecFunction();
		}
	}
}

void EmulatorView::Draw(IGraphics& g) {
	if (_plug && _plug->active()) {
		MessageBus* bus = _plug->messageBus();

		// FIXME: This constant is the delta time between frames.
		// It is set to this because on windows iPlug doesn't go higher
		// than 30fps!  Should probably add some proper time calculation here.
		_lsdjKeyMap.update(bus, 33.3333333);

		size_t available = bus->video.readAvailable();
		if (available > 0) {
			// If we have multiple frames, skip to the latest
			if (available > VIDEO_FRAME_SIZE) {
				bus->video.advanceRead(available - VIDEO_FRAME_SIZE);
			}

			bus->video.read((char*)_videoScratch, VIDEO_FRAME_SIZE);

			// TODO: This is all a bit unecessary and should be handled in the SameBoy wrapper
			unsigned char* px = _videoScratch;
			for (int i = 0; i < VIDEO_WIDTH; i++) {
				for (int j = 0; j < VIDEO_HEIGHT; j++) {
					std::swap(px[0], px[2]);
					px[3] = 255;
					px += 4;
				}
			}
		}

		DrawPixelBuffer((NVGcontext*)g.GetDrawContext());
	}
}

void EmulatorView::DrawPixelBuffer(NVGcontext* vg) {
	if (_imageId == -1) {
		_imageId = nvgCreateImageRGBA(vg, VIDEO_WIDTH, VIDEO_HEIGHT, NVG_IMAGE_NEAREST, (const unsigned char*)_videoScratch);
	} else {
		nvgUpdateImage(vg, _imageId, (const unsigned char*)_videoScratch);
	}

	nvgBeginPath(vg);

	NVGpaint imgPaint = nvgImagePattern(vg, mRECT.L, mRECT.T, VIDEO_WIDTH * 2, VIDEO_HEIGHT * 2, 0, _imageId, _alpha);
	nvgRect(vg, mRECT.L, mRECT.T, mRECT.W(), mRECT.H());
	nvgFillPaint(vg, imgPaint);
	nvgFill(vg);
}

IPopupMenu* createModelMenu(bool addElipses) {
	std::string elipses = addElipses ? "..." : "";

	IPopupMenu* menu = new IPopupMenu();
	menu->AddItem(("DMG B" + elipses).c_str(), (int)GameboyModel::DmgB);
	menu->AddItem(("CGB C" + elipses).c_str(), (int)GameboyModel::CgbC);
	menu->AddItem(("CGB E (default)" + elipses).c_str(), (int)GameboyModel::CgbE);
	/*menu->AddItem(("SGB NTSC" + elipses).c_str(), (int)GameboyModelMenuItems::SgbNtsc);
	menu->AddItem(("SGB PAL" + elipses).c_str(), (int)GameboyModelMenuItems::SgbPal);
	menu->AddItem(("SGB2" + elipses).c_str(), (int)GameboyModelMenuItems::Sgb2);*/
	menu->AddItem(("AGB" + elipses).c_str(), (int)GameboyModel::Agb);
	return menu;
}

enum class SystemMenuItems : int {
	LoadRom,
	LoadRomAs,
	Reset,
	ResetAs,

	Sep1,

	NewSram,
	LoadSram,
	SaveSram,
	SaveSramAs,
};

enum class SongMenuItems {
	Export,
	Load,
	Delete
};

IPopupMenu* createSongMenu(bool working) {
	IPopupMenu* menu = new IPopupMenu(0, true);
	menu->AddItem("Export .lsdsng...", (int)SongMenuItems::Export);
	menu->AddItem("Load (and reset)", (int)SongMenuItems::Load, working ? IPopupMenu::Item::kDisabled : 0);
	menu->AddItem("Delete (and reset)", (int)SongMenuItems::Delete, working ? IPopupMenu::Item::kDisabled : 0);
	return menu;
}

IPopupMenu* createSyncMenu(bool disableSync, bool autoPlay) {
	int flag = disableSync ? IPopupMenu::Item::kDisabled : 0;
	IPopupMenu* syncMenu = new IPopupMenu();
	syncMenu->AddItem("Off", LsdjSyncModeMenuItems::Off, flag);
	syncMenu->AddItem("MIDI Sync", LsdjSyncModeMenuItems::MidiSync, flag);
	syncMenu->AddItem("MIDI Sync (Arduinoboy Mode)", LsdjSyncModeMenuItems::MidSyncArduinoboy, flag);
	syncMenu->AddItem("MIDI Map", LsdjSyncModeMenuItems::MidiMap, flag);
	syncMenu->AddSeparator(LsdjSyncModeMenuItems::Sep1);
	syncMenu->AddItem("Auto Play", LsdjSyncModeMenuItems::AutoPlay, autoPlay ? IPopupMenu::Item::kChecked : 0);
	return syncMenu;
}

void EmulatorView::CreateMenu(float x, float y) {
	if (!_plug) {
		return;
	}

	std::string romName = _plug->romName();
	bool loaded = !romName.empty();
	if (!loaded) {
		romName = "No ROM Loaded";
	}

	_menu = IPopupMenu();

	IPopupMenu* systemMenu = CreateSystemMenu(loaded);
	
	_menu.AddItem(romName.c_str(), (int)RootMenuItems::RomName, IPopupMenu::Item::kTitle);
	_menu.AddSeparator((int)RootMenuItems::Sep1);

	OnProjectMenuRequest(&_menu, loaded);
	_menu.AddItem("System", systemMenu, (int)RootMenuItems::System);

	systemMenu->SetFunction([this](int indexInMenu, IPopupMenu::Item * itemChosen) {
		switch ((SystemMenuItems)indexInMenu) {
		case SystemMenuItems::LoadRom: OpenLoadRomDialog(GameboyModel::Auto); break;
		case SystemMenuItems::Reset: ResetSystem(); break;
		case SystemMenuItems::NewSram: break;
		case SystemMenuItems::LoadSram: OpenLoadSramDialog(); break;
		case SystemMenuItems::SaveSram: _plug->saveBattery(""); break;
		case SystemMenuItems::SaveSramAs: OpenSaveSramDialog(); break;
		}
	});

	if (loaded) {
		IPopupMenu* settingsMenu = CreateSettingsMenu();

		_menu.AddItem("Settings", settingsMenu, (int)RootMenuItems::Settings);
		_menu.AddSeparator((int)RootMenuItems::Sep2);
		_menu.AddItem("Game Link", (int)RootMenuItems::GameLink, _plug->gameLink() ? IPopupMenu::Item::kChecked : 0);
		_menu.AddSeparator((int)RootMenuItems::Sep3);

		_menu.SetFunction([this](int indexInMenu, IPopupMenu::Item * itemChosen) {
			switch ((RootMenuItems)indexInMenu) {
			case RootMenuItems::KeyboardMode: ToggleKeyboardMode(); break;
			case RootMenuItems::GameLink: _plug->setGameLink(!_plug->gameLink()); _manager->updateLinkTargets(); break;
			case RootMenuItems::SendClock: _plug->setMidiSync(!_plug->midiSync()); break;
			}
		});

		settingsMenu->SetFunction([this, settingsMenu](int indexInMenu, IPopupMenu::Item * itemChosen) {
			if (indexInMenu == settingsMenu->NItems() - 1) {
				ShellExecute(NULL, NULL, getContentPath().c_str(), NULL, NULL, SW_SHOWNORMAL);
			}
		});

		Lsdj& lsdj = _plug->lsdj();
		if (lsdj.found) {
			IPopupMenu* syncMenu = createSyncMenu(_plug->gameLink(), lsdj.autoPlay);
			_menu.AddItem("LSDj Sync", syncMenu, (int)RootMenuItems::LsdjModes);

			std::vector<LsdjSongName> songNames;
			lsdj.getSongNames(songNames);

			if (!songNames.empty()) {
				IPopupMenu* songMenu = new IPopupMenu();
				songMenu->AddItem("Import (and reset)...");
				songMenu->AddSeparator();

				_menu.AddItem("LSDj Songs", songMenu, (int)RootMenuItems::LsdjSongs);

				for (size_t i = 0; i < songNames.size(); i++) {
					IPopupMenu* songItemMenu = createSongMenu(songNames[i].projectId == -1);
					songMenu->AddItem(songNames[i].name.c_str(), songItemMenu);

					songItemMenu->SetFunction([=](int indexInMenu, IPopupMenu::Item * itemChosen) {
						int id = songNames[i].projectId;
						switch ((SongMenuItems)indexInMenu) {
						case SongMenuItems::Export: ExportSong(id); break;
						case SongMenuItems::Load: LoadSong(id); break;
						case SongMenuItems::Delete: DeleteSong(id); break;
						}
					});
				}
			}
			
			_menu.AddItem("Keyboard Shortcuts", (int)RootMenuItems::KeyboardMode, lsdj.keyboardShortcuts ? IPopupMenu::Item::kChecked : 0);

			int selectedMode = GetLsdjModeMenuItem(lsdj.syncMode);
			syncMenu->CheckItem(selectedMode, true);
			syncMenu->SetFunction([this](int indexInMenu, IPopupMenu::Item * itemChosen) {
				LsdjSyncModeMenuItems menuItem = (LsdjSyncModeMenuItems)indexInMenu;
				if (menuItem <= LsdjSyncModeMenuItems::MidiMap) {
					_plug->lsdj().syncMode = GetLsdjModeFromMenu(menuItem);
				} else {
					_plug->lsdj().autoPlay = !_plug->lsdj().autoPlay;
				}
			});
		} else {
			_menu.AddItem("Send MIDI Clock", (int)RootMenuItems::SendClock, _plug->midiSync() ? IPopupMenu::Item::kChecked : 0);
		}
	}

	GetUI()->CreatePopupMenu(*((IControl*)this), _menu, x, y);
}

IPopupMenu* EmulatorView::CreateSettingsMenu() {
	IPopupMenu* settingsMenu = new IPopupMenu();

	// TODO: These should be moved in to the SameBoy wrapper
	std::map<std::string, std::vector<std::string>> settings;
	settings["Color Correction"] = {
		"Off",
		"Correct Curves",
		"Emulate Hardware",
		"Preserve Brightness"
	};

	settings["High-pass Filter"] = {
		"Off",
		"Accurate",
		"Remove DC Offset"
	};

	for (auto& setting : settings) {
		const std::string& name = setting.first;
		IPopupMenu* settingMenu = new IPopupMenu(0, true);
		for (size_t i = 0; i < setting.second.size(); i++) {
			auto& option = setting.second[i];
			settingMenu->AddItem(option.c_str(), i);
		}

		settingMenu->CheckItem(_settings[name], true);
		settingsMenu->AddItem(name.c_str(), settingMenu);
		settingMenu->SetFunction([this, name](int indexInMenu, IPopupMenu::Item* itemChosen) {
			_settings[name] = indexInMenu;
			_plug->setSetting(name, indexInMenu);
		});
	}

	settingsMenu->AddSeparator();
	settingsMenu->AddItem("Open Settings Folder...");

	return settingsMenu;
}

IPopupMenu* EmulatorView::CreateSystemMenu(bool loaded) {
	IPopupMenu* loadAsModel = createModelMenu(true);

	IPopupMenu* menu = new IPopupMenu();
	menu->AddItem("Load ROM...", (int)SystemMenuItems::LoadRom);
	menu->AddItem("Load ROM As", loadAsModel, (int)SystemMenuItems::LoadRomAs);
	if (loaded) {
		IPopupMenu* resetAsModel = createModelMenu(false);
		menu->AddItem("Reset", (int)SystemMenuItems::Reset);
		menu->AddItem("Reset As", createModelMenu(false), (int)SystemMenuItems::ResetAs);
		menu->AddSeparator();

		menu->AddItem("New .sav", (int)SystemMenuItems::NewSram);
		menu->AddItem("Load .sav (and reset)...", (int)SystemMenuItems::LoadSram);
		menu->AddItem("Save .sav", (int)SystemMenuItems::SaveSram);
		menu->AddItem("Save .sav As...", (int)SystemMenuItems::SaveSramAs);

		resetAsModel->SetFunction([](int idx, IPopupMenu::Item*) {
			switch ((GameboyModel)idx) {
				
			}
		});
	}

	loadAsModel->SetFunction([=](int idx, IPopupMenu::Item*) {
		OpenLoadRomDialog((GameboyModel)(idx + 1));
	});

	return menu;
}

void EmulatorView::ToggleKeyboardMode() {
	_plug->lsdj().keyboardShortcuts = !_plug->lsdj().keyboardShortcuts;
}

void EmulatorView::ShowText(bool show) {
	if (_showText == show) {
		return;
	}

	const IRECT b = GetRECT();
	float mid = b.H() / 2;
	IRECT topRow(b.L, mid - 25, b.R, mid);
	IRECT bottomRow(b.L, mid, b.R, mid + 25);

	if (show) {
		assert(!_textIds[0]);
		_textIds[0] = new ITextControl(topRow, "Double click to", IText(23, COLOR_WHITE));
		_textIds[1] = new ITextControl(bottomRow, "load a ROM...", IText(23, COLOR_WHITE));

		GetUI()->AttachControl(_textIds[0]);
		GetUI()->AttachControl(_textIds[1]);
	} else {
		assert(_textIds[0]);
		GetUI()->DetachControl(_textIds[0], true);
		GetUI()->DetachControl(_textIds[1], true);
		_textIds[0] = nullptr;
		_textIds[1] = nullptr;
	}

	_showText = show;
}

void EmulatorView::UpdateTextPosition() {
	if (_showText) {
		assert(_textIds[0]);

		const IRECT b = GetRECT();
		float mid = b.H() / 2;
		IRECT topRow(b.L, mid - 25, b.R, mid);
		IRECT bottomRow(b.L, mid, b.R, mid + 25);

		_textIds[0]->SetTargetAndDrawRECTs(topRow);
		_textIds[1]->SetTargetAndDrawRECTs(bottomRow);
	}
}

void EmulatorView::ExportSong(int index) {
	std::vector<FileDialogFilters> types = {
		{ L"LSDj Songs", L"*.lsdsng" }
	};

	std::wstring path = BasicFileSave(types);
	if (path.size() == 0) {
		return;
	}

	Lsdj& lsdj = _plug->lsdj();
	if (lsdj.found) {
		lsdj.saveData.clear();
		_plug->saveBattery(lsdj.saveData);

		if (lsdj.saveData.size() > 0) {
			std::vector<char> songData;
			lsdj.exportSong(index, songData);

			if (songData.size() > 0) {
				writeFile(path, songData);
			}
		}
	}
}

void EmulatorView::LoadSong(int index) {
	Lsdj& lsdj = _plug->lsdj();
	if (lsdj.found) {
		lsdj.loadSong(index);
		_plug->loadBattery(lsdj.saveData, true);
	}
}

void EmulatorView::DeleteSong(int index) {
	Lsdj& lsdj = _plug->lsdj();
	if (lsdj.found) {
		lsdj.deleteSong(index);
		_plug->loadBattery(lsdj.saveData, true);
	}
}

void EmulatorView::ResetSystem() {
	_plug->reset();
}

void EmulatorView::OpenLoadSongsDialog() {
	std::vector<FileDialogFilters> types = {
		{ L"LSDj Songs", L"*.lsdsng" }
	};

	std::vector<std::wstring> paths = BasicFileOpen(types, true);
	Lsdj& lsdj = _plug->lsdj();
	if (lsdj.found) {
		lsdj.importSongs(paths);
		_plug->loadBattery(lsdj.saveData, true);
	}
}

void EmulatorView::OpenLoadRomDialog(GameboyModel model) {
	std::vector<FileDialogFilters> types = {
		{ L"GameBoy Roms", L"*.gb;*.gbc" }
	};

	std::vector<std::wstring> paths = BasicFileOpen(types, false);
	if (paths.size() > 0) {
		std::string p = ws2s(paths[0]);
		ShowText(false);
		_plug->init(p.c_str(), model);
	}
}

void EmulatorView::OpenLoadSramDialog() {
	std::vector<FileDialogFilters> types = {
		{ L"GameBoy Saves", L"*.sav" }
	};

	std::vector<std::wstring> paths = BasicFileOpen(types, false);
	if (paths.size() > 0) {
		std::string p = ws2s(paths[0]);
		_plug->loadBattery(p, true);
	}
}

void EmulatorView::OpenSaveSramDialog() {
	std::vector<FileDialogFilters> types = {
		{ L"GameBoy Saves", L"*.sav" }
	};

	std::wstring path = BasicFileSave(types);
	if (path.size() > 0) {
		std::string p = ws2s(path);
		_plug->saveBattery(p);
	}
}
