// OSP parity R1 (docs/OSP_PARITY_AUDIT.md): the shared ceramic menus (OSP's SharedMenuLook,
// MenuIcons; docs/research/menu-visual in OSP) and the shell's preset saving, in ANDOR ANALOG.

#include "AnalogEngine.h"
#include "../Source/FxRouting.h"   // ANALOG's own routing view (OSP's shared/andor-ui has one of the same name)
#include "../Source/SampleSourceUI.h"
#include "MainSections.h"
#include "MenuIcons.h"
#include "OspLookAndFeel.h"
#include "ShellEditor.h"
#include "ShellProcessor.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_cryptography/juce_cryptography.h>

#if JUCE_LINUX
 #include <dlfcn.h>
#endif

#if ! (JUCE_MAC || JUCE_IOS || JUCE_ANDROID)
namespace juce::detail
{
bool dispatchNextMessageOnSystemQueue (bool returnIfNoPendingMessages);   // (juce_events: Linux / Windows)
}
#endif

#include <map>
#include <set>

namespace
{
    using andor::shell::ShellEditor;
    using andor::shell::ShellProcessor;

    void setValue (ShellProcessor& p, const juce::String& id, float value)
    {
        auto* param = p.parameters.getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (value));
    }

    std::map<juce::String, float> allValues (ShellProcessor& p)
    {
        std::map<juce::String, float> values;
        for (auto* param : p.getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
                values[ranged->getParameterID()] = ranged->getValue();
        return values;
    }

    /** Documents/<product> in a folder of its own (ANDOR_DOCUMENTS_DIR), removed afterwards;
        the folder the test binary set for every test (tests/shell main) comes back after. */
    struct IsolatedDocuments
    {
        juce::String previous = juce::SystemStats::getEnvironmentVariable ("ANDOR_DOCUMENTS_DIR", {});
        juce::File dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                             .getChildFile ("analog-parity-" + juce::String (juce::Random::getSystemRandom().nextInt64()));
        static void put (const juce::String& value)
        {
           #if JUCE_WINDOWS
            ::_putenv_s ("ANDOR_DOCUMENTS_DIR", value.toRawUTF8());
           #else
            if (value.isEmpty())
                ::unsetenv ("ANDOR_DOCUMENTS_DIR");
            else
                ::setenv ("ANDOR_DOCUMENTS_DIR", value.toRawUTF8(), 1);
           #endif
        }
        IsolatedDocuments()
        {
            dir.createDirectory();
            put (dir.getFullPathName());
        }
        ~IsolatedDocuments()
        {
            put (previous);
            dir.deleteRecursively();
        }
    };

    struct Item
    {
        juce::String text;
        bool header = false, separator = false, hasImage = false, enabled = true, submenu = false;
        juce::String shortcut;
        juce::Colour colour;
    };

    std::vector<Item> itemsOf (const juce::PopupMenu& menu)
    {
        std::vector<Item> items;
        for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
        {
            const auto& i = it.getItem();
            items.push_back ({ i.text, i.isSectionHeader, i.isSeparator, i.image != nullptr || i.customComponent != nullptr, i.isEnabled,
                               i.subMenu != nullptr, i.shortcutKeyDescription, i.colour });
        }
        return items;
    }

    const Item* find (const std::vector<Item>& items, const juce::String& text)
    {
        for (const auto& i : items)
            if (i.text == text)
                return &i;
        return nullptr;
    }

    /** Every action of a menu carries an icon (or the icon column's blank), as OSP's: no row
        without its column. */
    void checkIconColumn (const juce::PopupMenu& menu, const char* what)
    {
        for (const auto& i : itemsOf (menu))
            if (! i.header && ! i.separator)
            {
                INFO (what << ": " << i.text);
                CHECK (i.hasImage);
            }
    }

    struct Rig
    {
        ShellProcessor p;
        std::unique_ptr<juce::AudioProcessorEditor> editor { p.createEditorIfNeeded() };
        ShellEditor& ui = dynamic_cast<ShellEditor&> (*editor);
        Rig()
        {
            editor->setSize (1448, juce::roundToInt (ui.instrumentHeight()));
            ui.refreshNow();
        }
        juce::PopupMenu mainMenu()
        {
            juce::PopupMenu menu;
            ui.showMenuForTest = [&] (juce::PopupMenu& m) { menu = m; };
            ui.openMainMenu();
            ui.showMenuForTest = nullptr;
            return menu;
        }
        osp::plugin::PresetBar& presetBar() { return dynamic_cast<osp::plugin::PresetBar&> (ui.presetBarComponent()); }
    };

    bool pumpMessages()
    {
       #if ! (JUCE_MAC || JUCE_IOS || JUCE_ANDROID)
        for (int i = 0; i < 200 && juce::detail::dispatchNextMessageOnSystemQueue (true); ++i)
        {
        }
        return true;
       #else
        return false;
       #endif
    }

   #if JUCE_LINUX
    /** Xvfb with no window manager lacks the _NET_WM_* atoms JUCE looks up (only if they exist)
        when it makes a window (as OSP's popover tests). */
    void prepareXServer()
    {
        auto* x11 = dlopen ("libX11.so.6", RTLD_NOW);
        if (x11 == nullptr)
            return;
        using Open = void* (*) (const char*);
        using Intern = unsigned long (*) (void*, const char*, int);
        using Close = int (*) (void*);
        auto open = reinterpret_cast<Open> (dlsym (x11, "XOpenDisplay"));
        auto intern = reinterpret_cast<Intern> (dlsym (x11, "XInternAtom"));
        auto close = reinterpret_cast<Close> (dlsym (x11, "XCloseDisplay"));
        if (open == nullptr || intern == nullptr || close == nullptr)
            return;
        if (auto* d = open (nullptr))
        {
            for (const auto* name : { "WM_PROTOCOLS", "WM_TAKE_FOCUS", "WM_DELETE_WINDOW", "_NET_WM_PING", "WM_CHANGE_STATE", "WM_STATE",
                                      "_NET_WM_WINDOW_TYPE", "_NET_WM_STATE", "_NET_WM_STATE_HIDDEN", "_NET_WM_STATE_FULLSCREEN",
                                      "_NET_WM_MOVERESIZE", "_NET_FRAME_EXTENTS", "_NET_WORKAREA", "_MOTIF_WM_HINTS", "_WIN_HINTS",
                                      "KWM_WIN_DECORATION", "_KDE_NET_WM_WINDOW_TYPE_OVERRIDE", "_NET_WM_WINDOW_TYPE_NORMAL",
                                      "_NET_WM_WINDOW_TYPE_DIALOG", "_NET_WM_STATE_SKIP_TASKBAR", "_NET_WM_STATE_ABOVE",
                                      "_NET_WM_WINDOW_TYPE_TOOLTIP", "_NET_WM_WINDOW_TYPE_COMBO", "_NET_WM_WINDOW_TYPE_POPUP_MENU",
                                      "_NET_WM_PID", "_NET_WM_ICON", "_NET_WM_NAME", "UTF8_STRING", "_NET_ACTIVE_WINDOW",
                                      "_NET_WM_STATE_MAXIMIZED_VERT", "_NET_WM_STATE_MAXIMIZED_HORZ", "_NET_WM_WINDOW_OPACITY",
                                      "_NET_SUPPORTED", "_NET_WM_ALLOWED_ACTIONS" })
                intern (d, name, 0);
            close (d);
        }
    }
   #endif

    bool haveDisplay()
    {
       #if JUCE_LINUX
        return juce::SystemStats::getEnvironmentVariable ("DISPLAY", {}).isNotEmpty();
       #else
        return false;
       #endif
    }

    bool isMenuWindow (juce::Component& c) { return juce::String (typeid (c).name()).contains ("MenuWindow") && c.isVisible(); }

    std::vector<juce::Component*> menuWindows (juce::Component& editor)
    {
        std::vector<juce::Component*> found;
        auto& desktop = juce::Desktop::getInstance();
        for (int i = 0; i < desktop.getNumComponents(); ++i)
            if (auto* c = desktop.getComponent (i); c != nullptr && isMenuWindow (*c))
                found.push_back (c);
        for (auto* child : editor.getChildren())
            if (isMenuWindow (*child))
                found.push_back (child);
        return found;
    }

    /** The editor around the open menus, each menu window drawn where it stands on screen. */
    void shot (juce::Component& editor, juce::Component* anchor, const juce::File& file)
    {
        auto windows = menuWindows (editor);
        // (Screen to the editor's own units: a scaled editor's local space is the unscaled one.)
        auto inEditor = [&] (juce::Component* w) { return w->getParentComponent() == &editor ? w->getBounds() : editor.getLocalArea (nullptr, w->getScreenBounds()); };
        juce::Rectangle<int> area;
        for (auto* w : windows)
            area = area.isEmpty() ? inEditor (w) : area.getUnion (inEditor (w));
        if (anchor != nullptr)
            area = area.getUnion (editor.getLocalArea (anchor->getParentComponent(), anchor->getBounds()));
        area = (area.isEmpty() ? editor.getLocalBounds() : area.expanded (28)).getIntersection (editor.getLocalBounds());
        constexpr float scale = 2.0f;
        auto image = editor.createComponentSnapshot (area, true, scale);
        {
            juce::Graphics g (image);
            for (auto* w : windows)
                if (w->getParentComponent() != &editor)
                {
                    const auto b = inEditor (w);
                    const auto picture = w->createComponentSnapshot (w->getLocalBounds(), true, scale * static_cast<float> (b.getWidth()) / static_cast<float> (w->getWidth()));
                    g.drawImage (picture, juce::Rectangle<int> (b.getWidth(), b.getHeight()).withPosition (b.getPosition() - area.getPosition()).toFloat() * scale);
                }
        }
        file.deleteFile();
        juce::FileOutputStream out (file);
        juce::PNGImageFormat().writeImageToStream (image, out);
        juce::PopupMenu::dismissAllActiveMenus();
        pumpMessages();
    }

    void hoverRow (juce::Component& window, int index)
    {
        std::vector<juce::Component*> rows;
        for (auto* c : window.getChildren())
            if (c->isVisible())
                rows.push_back (c);
        std::sort (rows.begin(), rows.end(), [] (auto* a, auto* b) { return a->getY() < b->getY(); });
        if (index >= static_cast<int> (rows.size()))
            return;
        juce::Desktop::getInstance().getMainMouseSource().setScreenPosition (rows[static_cast<std::size_t> (index)]->getScreenBounds().getCentre().toFloat());
        for (int i = 0; i < 8; ++i)
        {
            juce::Thread::sleep (40);
            pumpMessages();
        }
    }
}

TEST_CASE ("parity menus: the ANDOR look is every menu's while ANALOG's editor is open, and only then", "[analog][menus][parity]")
{
    auto shared = [] { return dynamic_cast<osp::plugin::OspLookAndFeel*> (&juce::LookAndFeel::getDefaultLookAndFeel()) != nullptr; };
    CHECK_FALSE (shared());
    {
        Rig rig;
        CHECK (shared());
    }
    CHECK_FALSE (shared());
}

TEST_CASE ("parity menus: ANALOG's global, preset and FX routing menus carry the shared icons; Save and Save As; shortcuts shown", "[analog][menus][parity]")
{
    IsolatedDocuments documents;
    Rig rig;
    const auto main = rig.mainMenu();
    checkIconColumn (main, "global menu");
    const auto items = itemsOf (main);
    const auto ellipsis = juce::String::fromUTF8 ("\xe2\x80\xa6");
    REQUIRE (find (items, "Save preset") != nullptr);
    REQUIRE (find (items, "Save preset as" + ellipsis) != nullptr);
    CHECK (find (items, "Save preset")->enabled);   // (no preset open: SAVE asks for a name)
    CHECK (find (items, "Undo")->shortcut.isNotEmpty());
    CHECK (find (items, "Redo")->shortcut.isNotEmpty());
    CHECK (find (items, "Save preset" + ellipsis) == nullptr);   // (the old single item is gone)

    // The preset dropdown: icons (presets keep the column); no "Save starting state" ANALOG has
    // nothing behind (the shell sets no handler for it).
    const auto presets = rig.presetBar().presetMenu();
    checkIconColumn (presets, "preset menu");
    for (const auto& i : itemsOf (presets))
        CHECK_FALSE (i.text.startsWithIgnoreCase ("Save starting state"));

    // FX ROUTING's gear (ANALOG's own view): every item its icon.
    rig.ui.sourceViewToggle().onClick();
    rig.ui.refreshNow();
    auto* view = dynamic_cast<andor::analog::FxRoutingView*> (rig.ui.sourceViewComponent());
    REQUIRE (view != nullptr);
    checkIconColumn (view->settingsMenu(), "FX routing gear");
}

TEST_CASE ("parity presets: factory presets first, then the musician's; SAVE updates only the musician's own, never a factory preset", "[analog][presets][parity]")
{
    IsolatedDocuments documents;
    ShellProcessor p;
    const auto factory = p.factoryPresetFolder();
    REQUIRE (factory.isDirectory());
    const auto list = p.presetList();
    REQUIRE (list.size() >= 2);
    // A preset of one's own (Save As into the presets folder).
    const auto mine = p.presetFolder().getChildFile ("AAA My Pad" + andor::shell::productInfo().presetExtension);
    setValue (p, "gain", -9.0f);
    REQUIRE (p.savePreset (mine));
    CHECK (p.currentPresetFile() == mine);
    CHECK (p.canSaveInPlace());
    // The list: the factory set first (every one marked factory), then the musician's.
    const auto withMine = p.presetList();
    REQUIRE (withMine.size() == list.size() + 1);
    bool seenUser = false;
    for (const auto& e : withMine)
    {
        INFO (e.file.getFullPathName());
        const bool isFactory = e.file.isAChildOf (factory);
        CHECK (e.context.upToFirstOccurrenceOf ("|", false, false) == (isFactory ? "origin:factory" : "origin:user"));
        CHECK_FALSE ((isFactory && seenUser));   // (no factory preset after a musician's)
        seenUser = seenUser || ! isFactory;
    }
    CHECK (withMine.back().file == mine);

    // The quick menu: PRESETS, then Factory and User as submenus (the folders inside them).
    {
        std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditorIfNeeded());
        auto& ui = dynamic_cast<ShellEditor&> (*e);
        auto& bar = dynamic_cast<osp::plugin::PresetBar&> (ui.presetBarComponent());
        juce::StringArray headers, submenus;
        const auto menu = bar.presetMenu();
        for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
        {
            const auto& item = it.getItem();
            if (item.isSectionHeader)
                headers.add (item.text);
            else if (item.subMenu != nullptr)
                submenus.add (item.text);
        }
        CHECK (headers.contains ("Presets"));
        CHECK (submenus.indexOf ("Factory") >= 0);
        CHECK (submenus.indexOf ("Factory") < submenus.indexOf ("User"));
    }

    // A factory preset opened: SAVE cannot update it in place (it asks for a name instead).
    REQUIRE (p.loadPreset (list.front().file));
    CHECK (p.isFactoryPreset (p.currentPresetFile()));
    CHECK_FALSE (p.canSaveInPlace());
    // INIT: no file, nothing to update.
    p.initPatch();
    CHECK_FALSE (p.canSaveInPlace());
}

TEST_CASE ("parity presets: a saved preset reloads exactly, and SAVE in place writes the musician's file only", "[analog][presets][parity]")
{
    IsolatedDocuments documents;
    ShellProcessor p;
    const auto ext = andor::shell::productInfo().presetExtension;
    const auto file = p.presetFolder().getChildFile ("Exact" + ext);
    setValue (p, "gain", -7.5f);
    setValue (p, "arp.enabled", 1.0f);
    setValue (p, "arp.strum.on", 1.0f);
    setValue (p, "analog.arp.cardC.strum.ms", 120.0f);
    REQUIRE (p.savePreset (file));
    const auto saved = allValues (p);
    // Everything moved away, then the preset again: every parameter as saved.
    p.initPatch();
    REQUIRE (allValues (p) != saved);
    REQUIRE (p.loadPreset (file));
    const auto loaded = allValues (p);
    for (const auto& [id, v] : saved)
    {
        INFO (id);
        CHECK (loaded.at (id) == Catch::Approx (v).margin (1.0e-6));
    }
    // SAVE in place (the editor's SAVE): the same file, now with the change; the factory set untouched.
    std::map<juce::String, juce::String> factoryBefore;
    for (const auto& f : p.factoryPresetFolder().findChildFiles (juce::File::findFiles, true))
        factoryBefore[f.getFullPathName()] = juce::SHA256 (f).toHexString();
    REQUIRE (p.canSaveInPlace());
    setValue (p, "gain", -3.0f);
    REQUIRE (p.savePreset (p.currentPresetFile()));
    ShellProcessor q;
    REQUIRE (q.loadPreset (file));
    CHECK (q.parameters.getParameter ("gain")->convertFrom0to1 (q.parameters.getParameter ("gain")->getValue()) == Catch::Approx (-3.0f).margin (0.01));
    for (const auto& [path, hash] : factoryBefore)
        CHECK (juce::SHA256 (juce::File (path)).toHexString() == hash);
}

TEST_CASE ("parity menus: the card sits on its anchor and a submenu opens edge to edge in ANALOG, at 100, 150 and 200 %", "[analog][menus][parity]")
{
    if (! haveDisplay())
        SKIP ("needs a window system (a Linux display; Xvfb is enough)");
   #if JUCE_LINUX
    prepareXServer();
   #endif
    osp::plugin::OspLookAndFeel::setTranslucentMenusForTests (true);
    IsolatedDocuments documents;
    Rig rig;
    auto& e = *rig.editor;
    e.setTopLeftPosition (0, 0);
    e.setVisible (true);
    e.addToDesktop (0);
    constexpr int margin = osp::plugin::OspLookAndFeel::popupShadowMargin;
    for (const float scale : { 1.0f, 1.5f, 2.0f })
    {
        INFO ("UI scale " << scale);
        e.setScaleFactor (scale);
        // The global menu from its own key: its card's right edge on the key's (as ANDOR OSP's
        // main menu), at every scale.
        {
            rig.ui.openMainMenu();
            for (int i = 0; i < 25 && menuWindows (e).empty(); ++i)
            {
                juce::Thread::sleep (40);
                pumpMessages();
            }
            const auto windows = menuWindows (e);
            REQUIRE (windows.size() == 1);
            const auto key = rig.ui.menuComponent().getScreenBounds();
            const auto card = windows.front()->getScreenBounds().reduced (juce::roundToInt (static_cast<float> (margin) * scale));
            // (Where the editor fits the screen at this scale: JUCE keeps a menu on screen.)
            const auto screenArea = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea;
            if (screenArea.contains (key))
                CHECK (std::abs (card.getRight() - key.getRight()) <= juce::roundToInt (2.0f * scale));
            CHECK (card.getY() >= key.getBottom() - 1);
            juce::PopupMenu::dismissAllActiveMenus();
            pumpMessages();
        }
        // The global menu (its Learn item opens a submenu), shown from the preset bar as an anchor.
        auto& bar = rig.ui.presetBarComponent();
        auto menu = rig.mainMenu();
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&bar).withMaximumNumColumns (1));
        auto windows = menuWindows (e);
        REQUIRE (windows.size() == 1);
        auto* top = windows.front();
        CHECK (dynamic_cast<osp::plugin::OspLookAndFeel*> (&top->getLookAndFeel()) != nullptr);
        CHECK_FALSE (top->isOpaque());
        const float drawn = juce::Component::getApproximateScaleFactorForComponent (&bar);
        const auto anchor = bar.getScreenBounds().toFloat() / drawn;
        const auto window = top->getBounds().toFloat();
        CHECK (window.getX() + static_cast<float> (margin) == Catch::Approx (anchor.getX()).margin (2.0));
        CHECK (window.getY() + static_cast<float> (margin) == Catch::Approx (anchor.getBottom() + 3.0f).margin (2.0));
        int index = 0, row = -1, presses = 0;
        for (juce::PopupMenu::MenuItemIterator it (menu); it.next(); ++index)
        {
            const auto& item = it.getItem();
            if (row < 0 && ! item.isSeparator && ! item.isSectionHeader && item.isEnabled)
                ++presses;
            if (item.text == "Learn")
                row = index;
        }
        REQUIRE (row >= 0);
        for (int k = 0; k < presses; ++k)
            top->keyPressed (juce::KeyPress (juce::KeyPress::downKey));
        top->keyPressed (juce::KeyPress (juce::KeyPress::rightKey));
        windows = menuWindows (e);
        REQUIRE (windows.size() == 2);
        auto* sub = windows.front() == top ? windows.back() : windows.front();
        CHECK (sub->getX() + margin == top->getRight() - margin - 3);
        std::vector<juce::Component*> rows;
        for (auto* c : top->getChildren())
            if (c->isVisible())
                rows.push_back (c);
        std::sort (rows.begin(), rows.end(), [] (auto* a, auto* b) { return a->getY() < b->getY(); });
        const int border = top->getLookAndFeel().getPopupMenuBorderSize();
        // (Level with its row, unless the screen is too short for it there: JUCE then moves a
        // submenu up to fit, as on any screen; ANALOG's global menu is long at 200 %.)
        // (Checked where the editor fits the screen at this scale: on a smaller screen JUCE keeps
        // a submenu inside the editor's visible part, as it should.)
        const auto screen = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea;
        const bool fits = static_cast<float> (e.getWidth()) * scale <= static_cast<float> (screen.getWidth())
                       && static_cast<float> (e.getHeight()) * scale <= static_cast<float> (screen.getHeight());
        if (fits)
            CHECK (std::abs (sub->getY() + border - (top->getY() + rows[static_cast<std::size_t> (row)]->getY())) <= 2);
        juce::PopupMenu::dismissAllActiveMenus();
        pumpMessages();
    }
    e.removeFromDesktop();
    osp::plugin::OspLookAndFeel::setTranslucentMenusForTests (false);
}

TEST_CASE ("parity menus: screenshots of ANALOG's menus in the shared look (ANALOG_MENU_SHOTS)", "[.][menu-parity-shots]")
{
    const char* dir = std::getenv ("ANALOG_MENU_SHOTS");
    if (dir == nullptr || ! haveDisplay())
        return;
   #if JUCE_LINUX
    prepareXServer();
   #endif
    const juce::File out (juce::String::fromUTF8 (dir));
    out.createDirectory();
    const float uiScale = std::getenv ("ANALOG_MENU_SCALE") != nullptr ? juce::String (std::getenv ("ANALOG_MENU_SCALE")).getFloatValue() : 1.0f;
    const juce::String suffix = uiScale == 1.0f ? juce::String() : "-" + juce::String (juce::roundToInt (uiScale * 100.0f));
    auto file = [&] (const char* name) { return out.getChildFile (juce::String ("analog-") + name + suffix + ".png"); };
    osp::plugin::OspLookAndFeel::setTranslucentMenusForTests (true);
    IsolatedDocuments documents;
    Rig rig;
    auto& e = *rig.editor;
    e.setTopLeftPosition (0, 0);
    e.setVisible (true);
    e.addToDesktop (0);
    e.setScaleFactor (uiScale);
    rig.ui.refreshNow();
    // A preset of one's own beside the factory set (both headings in the dropdown).
    rig.p.savePreset (rig.p.presetFolder().getChildFile ("My Night Pad" + andor::shell::productInfo().presetExtension));

    // 1. The global menu from its key (a row hovered).
    rig.ui.openMainMenu();
    for (int i = 0; i < 25 && menuWindows (e).empty(); ++i)
    {
        juce::Thread::sleep (40);
        pumpMessages();
    }
    if (const auto windows = menuWindows (e); ! windows.empty())
        hoverRow (*windows.front(), 3);
    shot (e, &rig.ui.menuComponent(), file ("01-global-menu"));
    // 2. The preset dropdown.
    {
        auto& bar = rig.presetBar();
        bar.presetMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&bar).withMaximumNumColumns (1));
        shot (e, &bar, file ("02-preset-menu"));
    }
    // 3. Card C's sound menu (SOUNDS: Kontakt imports, Remove).
    {
        auto& card = rig.ui.engineCard (1);
        andor::analog::showSoundMenu (rig.p, card, 1);
        shot (e, &card, file ("03-sound-menu"));
    }
    // 4. FX ROUTING's gear.
    rig.ui.sourceViewToggle().onClick();
    rig.ui.refreshNow();
    if (auto* view = dynamic_cast<andor::analog::FxRoutingView*> (rig.ui.sourceViewComponent()))
    {
        view->settingsMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&view->settingsButton()).withMaximumNumColumns (1));
        shot (e, &view->settingsButton(), file ("04-fx-routing-menu"));
    }
    rig.ui.sourceViewToggle().onClick();
    rig.ui.refreshNow();
    e.removeFromDesktop();
    osp::plugin::OspLookAndFeel::setTranslucentMenusForTests (false);
}


TEST_CASE ("parity presets: USER shows the musician's folders as submenus (subfolders first, then the folder's presets); the arrows walk the same order", "[analog][presets][parity]")
{
    IsolatedDocuments documents;
    ShellProcessor p;
    const auto ext = andor::shell::productInfo().presetExtension;
    const auto root = p.presetFolder();
    // Loose presets at the top, a pack with its families, and an empty folder (not listed).
    const auto loose = root.getChildFile ("Loose Lead" + ext);
    const auto packA = root.getChildFile ("Cinematic Pack/Basses/Deep One" + ext);
    const auto packB = root.getChildFile ("Cinematic Pack/Pads/Wide One" + ext);
    const auto packTop = root.getChildFile ("Cinematic Pack/Pack Intro" + ext);
    root.getChildFile ("Empty Folder").createDirectory();
    for (const auto& f : { loose, packA, packB, packTop })
    {
        f.getParentDirectory().createDirectory();
        REQUIRE (p.savePreset (f));
    }
    // The list (and the arrows): Factory first, then USER - Cinematic Pack (Basses, Pads, then
    // its own preset), then the loose presets.
    juce::Array<juce::File> users;
    for (const auto& e : p.presetList())
        if (! p.isFactoryPreset (e.file))
            users.add (e.file);
    CHECK (users == juce::Array<juce::File> ({ packA, packB, packTop, loose }));
    // The menu: User > Cinematic Pack > Basses > Deep One.
    std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditorIfNeeded());
    auto& ui = dynamic_cast<ShellEditor&> (*e);
    auto& bar = dynamic_cast<osp::plugin::PresetBar&> (ui.presetBarComponent());
    const auto menu = bar.presetMenu();
    auto childNamed = [] (const juce::PopupMenu& m, const juce::String& name) -> const juce::PopupMenu* {
        for (juce::PopupMenu::MenuItemIterator it (m); it.next();)
            if (it.getItem().text == name && it.getItem().subMenu != nullptr)
                return it.getItem().subMenu.get();
        return nullptr;
    };
    auto texts = [] (const juce::PopupMenu& m) {
        juce::StringArray t;
        for (juce::PopupMenu::MenuItemIterator it (m); it.next();)
            if (! it.getItem().isSeparator)
                t.add (it.getItem().text);
        return t;
    };
    const auto* user = childNamed (menu, "User");
    REQUIRE (user != nullptr);
    CHECK (texts (*user) == juce::StringArray ({ "Cinematic Pack", "Loose Lead" }));
    const auto* pack = childNamed (*user, "Cinematic Pack");
    REQUIRE (pack != nullptr);
    CHECK (texts (*pack) == juce::StringArray ({ "Basses", "Pads", "Pack Intro" }));
    const auto* basses = childNamed (*pack, "Basses");
    REQUIRE (basses != nullptr);
    CHECK (texts (*basses) == juce::StringArray ({ "Deep One" }));
    // The arrows from the pack's last preset go to the loose one.
    REQUIRE (p.loadPreset (packTop));
    p.stepPresetList (1);
    CHECK (p.currentPresetFile() == loose);
}
