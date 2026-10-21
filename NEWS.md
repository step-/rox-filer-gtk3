# rox-filer-gtk3 News File
<!-- let g:wiki_toc_depth=1|WikiTocGenerateLocal|let g:wiki_toc_depth=6 -->

*Contents*
* [Unreleased]
* [Beta 2 test 2026-10-21]
* [Beta 1 test 2026-10-14]
* [Alpha 3 test 2026-09-15]
* [Alpha 2 test 2026-09-09]
* [Alpha 1 test 2026-09-04]

---

**All together:**
[New since alpha 3] ·
[Changed since alpha 3] ·
[Fixed since alpha 3] ·
[New since alpha 2] ·
[Merged SendTo menus] ·
[Support selecting file names starting with a number] ·
[Show numbered groups in the bookmarks menu] ·
[Improved support for empty panels] ·
[Command-line --options] ·
[Command-line --exit] ·
[Fixed since alpha 2] ·
[Toolbar opaque under compositor] ·
[AppDir menu stock icons] ·
[Keyboard focus after minibuffer] ·
[Export ROX_FILER_CONFIG] ·
[Item menu icons and accelerators] ·
[Thumbnailers help] ·
[Manage thumbnails removed] ·
[Video thumbnails removed] ·
[Jun7 feature set] ·
[Same configuration files] ·
[Document scope] ·
[Isolated configuration] ·
[Sane defaults] ·
[Disable RPC handler] ·
[Select view type option] ·
[Show panel edge toggle] ·
[Per-window navigation history] ·
[Auto-resize floor for toolbar] ·
[Transparent pinboard label background] ·
[Thumbnail blending for JPEG save] ·
[Opaque theme background by default] ·
[Default file type colors] ·
[Custom shortcut editor] ·
[Custom SVG icon theme] ·
[Right-click menu App and SendTo submenus] ·
[Right-click menu pre-selection removed] ·
[Tooltip border removed] ·
[HiDPI-aware rendering] ·
[Widget names for CSS customization] ·
[Instance stylesheet] ·
[Stylesheet reload via SOAP] ·
[Keyboard focus indicator] ·
[Icon view selection] ·
[Rubberband selection] ·
[Wink outline] ·
[Minibuffer colors] ·

---

## Unreleased

## Beta 2 test 2026-10-21

No functional changes since beta 1. This is the first public beta test.

Commit: 6755ea2

## Beta 1 test 2026-10-14

*Contents*
* [New since alpha 3]
* [Changed since alpha 3]
* [Fixed since alpha 3]

Commit: 1c61d9a

### New since alpha 3

* New `--exit-force` option: quits without prompting on pending operations.
  It quits a blocked `--exit` without prompting. Over SOAP RPC both are the
  `Exit` method: without arguments it behaves as `--exit` (prompts if anything
  is open); with a `Force` argument of `yes` it behaves as `--exit-force`.

* The Find window has a draggable divider between the log and the results table.
  The results side never goes below 100 pixels. The divider cannot move into space
  the log has already grown into, so its upper stop is the log's current height.

  The Find results table supports multi-selection and has a right-click
  menu: Copy path (full paths, one per line), Export to table (a
  `Name | Directory` markdown file, defaulting to `~/results.md`), and
  Select all. Opening a location works unchanged, for a single selected row.

* The saved Options file carries a `version` attribute on its root element,
  starting at 1. Files without it are treated as legacy files. A newer
  version than the running filer understands warns and continues.

* The New Directory action creates missing parent directories, so a name
  like `/a/b/c` works. The new top-level directory is winked. A name that
  already exists still reports an error.

* Static submenu labels carry keyboard mnemonics: open a menu and press the
  underlined letter to enter its submenu. Saved shortcut customizations are
  unaffected. Translations with mnemonics are not available: entry activation
  by letter awaits translators.

* The Properties dialog has an "Icon" row showing the file the item's
  icon is loaded from. The row reports whichever source supplied the icon:
  a user-assigned icon, a `.DirIcon`, or a MIME-type icon. The error and
  the unknown-type icons show the file with a comment appended.

  Selecting the row copies its value to the primary selection, so the path
  can be pasted with a middle-click, as with any other row in the dialog.
  `Ctrl+C` and `Ctrl+V` do not work here: the value cell is read-only and
  the dialog never writes the clipboard.

* The filer's background menu has a "Pinboard" submenu, so the pinboard's
  backdrop and panel setup can be reached without right-clicking the
  pinboard. "Add Panel" works whether or not a pinboard is running;
  "Backdrop..." is shaded when there is none, since setting a backdrop
  needs one.

* The Numbered Groups submenu (Bookmarks menu) has a "Help topic" item
  opening the manual at "Saving and restoring the selection", and "Edit
  Groups.xml" now carries a keyboard mnemonic.

* The "File type colours" section of the Options Colours tab has a
  "Reset to defaults" button restoring the built-in colors,
  re-adapted for contrast against the current theme background.

### Changed since alpha 3

* Quitting with `--exit` while the Options, Properties, Shortcut, or
  Bookmarks editor is open now asks for confirmation; previously
  these dialogs stayed open.

  The windows below still close without asking:
  * Help pages (each opens from the Help button inside a working
    window): the Find expression help in the Find window, the
    Change permissions help in the Permissions window, the
    Set type help in the Set Type window, and the help text
    inside the Send-To menu customiser.
  * Questions and confirmations (popups that wait for an answer, then
    go away by themselves): "Symlink already exists, replace it?"
    when linking onto an existing link, every error and information
    message, and the "drag the icon to a directory viewer"
    notice. The quit-warning dialog itself.

* The Set backdrop dialog and the Edit Item dialog for a desktop icon
  no longer close quietly with the last filer window quit via `--exit`.
  When either is open now the filer asks for confirmation.

* The Panel Options dialog and the Remove Panel prompt no longer
  close quietly with the last filer window quit via `--exit`. When
  either is open now the filer asks for confirmation.

* Question dialogs that wait for an answer (delete and overwrite
  confirmations, unmount, run-action questions and the like) now
  ask for confirmation while open when quitting with `--exit`.

* Error and information popups now carry the title "ROX-Filer", the
  symlink-replace question "Symlink", and the unmount question
  "Unmount". Previously these opened without a title.

* Changed the default scrollbar CSS to prevent desktop bleed through
  with the Marwaita-X theme family. Update your custom style.css file.
  The default CSS is a compromize between various common themes, and
  could not exactly match the user's theme. Tweak it via style.css.

* Added some menu icons.

* The "Size of icons in menus" option now applies to the filer menus,
  the panel icon menu, the pinboard icon menu and an AppDir's own menu
  consistently. The option offers: Small (16 px), Large (24 px), Huge
  (32 px), and No Icons. Previously, the sizes of named icons and icons
  loaded from a file disagreed: named icons were always 16 pixels, while
  file icons followed the font size. Now No Icons removes icons from
  those menus rather than only icons from file.  At the default setting
  every icon is 16 pixels.

  The "Same as current window" and "Same as default" entries are
  withdrawn: menu icons no longer follow the icon view. Their
  stored values stay reserved and resolve to the default size.

* The "Shift Open" menu entry now shows the icon of the file, folder,
  symlink, or mount point under the pointer, matching the label
  (Unmount, Open unmounted, Show Target, Look Inside, Open As Text).
  With nothing under the pointer the entry shows no icon for "Shift Open".

* The pinboard and panel icon menus now hold the Send-To entries in a
  "Send To..." submenu instead of listing them flat at the top.
  AppDir entries, if any, remain flat above it.

* The Options "Icon Theme" pull-down selector leaves out the
  names of icon themes that miss a `Context=MimeTypes` declaration.

* A warning is emitted if the `~/.icons/ROX` directory is missing
  the required `index.theme` file.

* The Symlink window is wider.

* Cosmetic: dragging one or more items out of a filer window now grabs the
  icon by its middle-bottom instead of its top-left corner.

### Fixed since alpha 3

* Fixed the Duplicate, Rename, Symlink, plus the New Directory, New
  File and New File From Template choosers from the File menu, plus
  the Make-start-script chooser from the Options menu; as well as
  the Set Run Action, Set Icon, Bulk Rename, and Extended Attributes
  editors. Each no longer closes with the filer window that launched
  it. Each also blocks quitting with --exit until the user confirms.

* Reset to Defaults in the Shortcuts editor now clears exactly the shown
  shortcuts then applies defaults (including Fatdog); Reset and Revert save
  immediately when the save file already exists, and do not create a new
  file if the shortcut setup is pristine.

* Fixed Ctrl+V paste failing in some cases.

* A panel the window manager places aside of its edge (e.g. beside another
  dock) now moves back to its requested spot, then reserves space where it
  actually sits so maximized windows no longer slide under the panel. It is
  no longer moved off-screen (a bottom or right panel could disappear) and
  no longer flickers between the edge and the displaced position.

* Force-closing quitting with `--exit` while the unmount question is open
  now quits instead of leaving a windowless process running.

* The `--exit` confirmation prompt now opens above an already-open prompt
  (such as an error popup) instead of below it.

* The item Send To.. and New menus, the pinboard icon menu, and the filer menu now
  display visible dividers. Tweak via `menu separator` in the user's `style.css`.
  Dividers were added beside two Customise entries for uniformity.

* The submenus of a pinboard or panel icon's Send-To entries, e.g., the
  Encryption submenu, now display theirs entries. Previously, they were empty.

* The log area of the action windows now grows to suit the tool output,
  up to a third of the screen height, instead of staying at a fixed three
  lines, and scrolls to end. The windows are: Count, Delete, Copy, Move,
  Link, Permissions, Set type, Find, Mount / Unmount, and Eject.

* Send-To menu entry labels again underline the letter
  after underscore and activate on that letter, as in GTK2.
  App-menu and panel labels still show underscore literally.

* The dragged icon no longer shows a solid background when no compositor
  runs; the drag window is cut to the icon outline, as in GTK2.

* The pointer now stays over the thumbnail when dragging the icon out of a
  New Directory box, instead of riding off its left/right side. This fix
  extends to all similar dialog boxes.

## Alpha 3 test 2026-09-15

*Contents*
* [New since alpha 2]
* [Fixed since alpha 2]

Commit: ee03443

### New since alpha 2

*Contents*
* [Merged SendTo menus]
* [Support selecting file names starting with a number]
* [Show numbered groups in the bookmarks menu]
* [Improved support for empty panels]
* [Command-line --options]
* [Command-line --exit]

#### Merged SendTo menus

The File menu's `SendTo...` submenu and the `Send To...` pop-up entry are merged
into a single `Send To...` submenu showing file type handlers plus generic
SendTo handlers. The pop-up keeps working unchanged via Shift+right-click,
Shift+F10, Shift+`|` or Shift+Menu. In addition, a unified `Customise...`
entry names the context (file type, media type, or all files) and opens the
matching folder and the corresponding help dialog.

Note: If you had keyboard shortcuts bound to the old
`SendTo...` submenu or `Send To...` pop-up, they are no
longer working; use the predefined Shift shortcuts above.

#### Support selecting file names starting with a number

The new _Digits select, Alt+digit jumps to groups_ checkbox in the Options →
_Filer Windows_ window, resolves the clash between numbered groups and numbered
group selection. When enabled, typing a digit starts selecting names beginning
with that digit, while Alt+digit jumps to the numbered group. The default
disabled state of the new checkbox keeps the classic bindings where a bare digit
jumps to the numbered group. Group saving via Control+digit is unchanged.

#### Show numbered groups in the bookmarks menu

The Bookmarks menu has a Numbered Groups submenu listing the ten
numbered-group slots: active slots show their saved directory and saved
file count and jump to them when chosen; unset slots are greyed out.

How to try: save a group with Control+digit, open the Bookmarks menu
and check the _Numbered Groups_ submenu, then choose a row to jump to
it. To reset a slot, choose _Edit Groups.xml_ at the submenu bottom and
delete that slot's entry. Tip: you can also jump to numbered groups as
discussed in the [Support selecting file names starting with a number]
section.

#### Improved support for empty panels

Unlike the legacy ROX-Filer, empty panels do not shrink below the configured
thickness. This feature allows reserving desktop workareas using panels,
similarly to the Openbox `<margins>` tag, but with HiDPI-awareness.

How to try: Right-click the pinboard and open the bottom panel, right-click
the panel to open its options and set thickness. Remove all icons from the
panel. Reserve a bottom margin invoking ROX-Filer in a Startup file and
passing `--bottom=Bottom`.

#### Command-line --options

The new `--options` flag opens the Options dialog from the command line.
It works whether or not a filer is already running.  Used alone it does
not open the current directory alongside. Over SOAP RPC this is the
`ShowOptions` method.

#### Command-line --exit

The new `--exit` flag closes every window of the configuration: filer windows,
panels, pinboard, and dialogs. If copy/move and other action are
in progress, a warning offers to force-close (aborting them) or keep running
instead. Open editor dialogs (Options, Properties, Bookmarks, Shortcuts) close
without warning.

### Fixed since alpha 2

* Do not close the Properties and other action dialogs with the filer
  window; exception: help dialogs are still closed with the filer window
* Wrap long file names and link targets in Properties dialog
* Keep scroll position when unfocused window takes focus
* Ensure the Panel Options window moves to mouse
* Ensure non-applicable menu entries are greyed out
* Make selection colors theme-aware
* Restore the Shift+Menu SendTo shortcut

## Alpha 2 test 2026-09-09

*Contents*
* [Toolbar opaque under compositor]
* [AppDir menu stock icons]
* [Keyboard focus after minibuffer]
* [Export ROX_FILER_CONFIG]
* [Item menu icons and accelerators]
* [Thumbnailers help]
* [Manage thumbnails removed]
* [Video thumbnails removed]

Commit: 78113ba

### Toolbar opaque under compositor

The toolbar now keeps an opaque theme background when a compositor is
running. With flat themes that set transparent toolbars the desktop
wallpaper no longer bleeds through behind the toolbar buttons.

How to try: run with a flat theme and a compositor, for example
`GTK_THEME=Flat-grey-rounded` with compton, and open a filer window.

### AppDir menu stock icons

The AppDir menu restores legacy stock icons via the GTK stock factory,
shows the AppDir .DirIcon file when the icon name is .DirIcon, and adds
a help browser icon to the automatically added Help entry.

How to try: set icon to `.DirIcon` or a legacy stock name in AppInfo.xml.
Create a Help subdirectory to see the Help entry with its icon.

### Keyboard focus after minibuffer

Closing the minibuffer now returns keyboard focus to the file view, so
arrow keys work immediately after dismissing it.

### Export ROX_FILER_CONFIG

The filer now exports the effective `ROX_FILER_CONFIG` value to child
processes, so handlers started from the filer can discover the correct
configuration directory and SOAP window.

How to try: run `AppRun --config=dev` and from a handler print the
`ROX_FILER_CONFIG` environment variable, or check the About dialog with
Show Configuration Files.

### Item menu icons and accelerators

The item menu now shows icons for all leaf entries, and shows keyboard
accelerators aligned on the right.

### Thumbnailers help

The Options dialog now includes help text for the thumbnailers section.

### Manage thumbnails removed

Breaking change — the Manage thumbnails cache button has been removed.
Thumbnail cache management is handled via the Purge Thumbnails Cache
button.

### Video thumbnails removed

Breaking change — the Video thumbnails option has been removed. The
legacy video thumbnail path relied on unavailable components.

## Alpha 1 test 2026-09-04

*Contents*
* [Jun7 feature set]
* [Same configuration files]
* [Document scope]
* [Isolated configuration]
* [Sane defaults]
* [Disable RPC handler]
* [Select view type option]
* [Show panel edge toggle]
* [Per-window navigation history]
* [Auto-resize floor for toolbar]
* [Transparent pinboard label background]
* [Thumbnail blending for JPEG save]
* [Opaque theme background by default]
* [Default file type colors]
* [Custom shortcut editor]
* [Custom SVG toolbar icon theme]
* [Right-click menu App and SendTo submenus]
* [Right-click menu pre-selection removed]
* [Tooltip border removed]
* [HiDPI-aware rendering]
* [Widget names for CSS customization]
* [Instance stylesheet]
* [Stylesheet reload via SOAP]
* [Keyboard focus indicator]
* [Icon view selection]
* [Rubberband selection]
* [Wink outline]
* [Minibuffer colors]

Commit: 0e2aeae

### Jun7 feature set

[This project] is a port of the jun7 enhanced ROX-Filer to GTK3, focusing on
correctness and compatibility with the legacy ROX-Filer (jun7's GTK2 version).

With few exceptions, current users of jun7 ROX-Filer should
feel right at home using rox-filer-gtk3; the main difference
is it now runs on the more capable and polished GTK3 toolkit.

**Note for [Forum] members**: jun7 ROX-Filer is similar but not quite the
same as the [woof-CE] ROX-Filer you have been using on your platform. Jun7
ROX-Filer is the Fatdog64 Linux filer since [Fatdog64 710 Beta] 29 August 2016.

So, expect very few new features of rox-filer-gtk3 (although some are in).
If you prefer a feature-rich ROX-Filer variant, look at the [ROX-Filer2] project.

### Same configuration files

By default rox-filer-gtk3 uses **the same** configuration directory and files of
legacy ROX-Filer, `~/.config/rox.sourceforge.net/ROX-Filer`. This was chosen to
simplify Fatdog64 platform migration. Configuration file names and formats are
fully compatible, although rox-filer-gtk3 writes new keys that the legacy filer
will ignore.

See also: [Isolated configuration], [Instance stylesheet].

---------------------------------------------------------------------
That said,

**BACK UP YOUR LEGACY CONFIGURATION BEFORE TESTING THIS NEW FILER**

---------------------------------------------------------------------

### Document scope

The remainder of this document introduces new features
and breaking changes with respect to the legacy jun7 filer.

### Isolated configuration

A new `--config=NAME` option and `ROX_FILER_CONFIG` environment variable
isolate the configuration directory. GTK2 and GTK3 instances can run side
by side with separate Options, panels and pinboards. However, since there
is only one screen, if both programs open pinboard or panels at the same
time, which program will win the screen is not predictable.

How to try: run `AppRun --config=dev` or set
export `ROX_FILER_CONFIG=dev` to use the configuration
directory `~/.config/rox.sourceforge.net/ROX-Filer-dev`.

How to check: Right-click an empty area to open the filer menu,
select Help > About and click the Show Configuration Files button.

### Sane defaults

Default options have been revised to match current desktop
expectations. Fresh installs start with opaque backgrounds, HiDPI-ready
icon sizes and compact toolbar layout.

How to try: Rename `~/.config/rox.sourceforge.net/ROX-Filer` and
launch ROX-Filer to see the new defaults. Or start ROX-Filer with an
empty isolated configuration: `AppRun --config=empty`.

### Disable RPC handler

The `-z` option stops the program creating its shared RPC message window. It
is for use with `-n` to ensure a new filer window runs in complete isolation.

### Select view type option

The `-V`, `--view-type` option opens a window with the specified view type.

How to try: run `AppRun -V 2 /tmp` to open `/tmp` in list view.

### Show panel edge toggle

A new Show panel edge option in Options > Panel adds a visible 1px
separator at the panel window edge and reserves 1px of workarea space.
Disabling the option reclaims the full height or width for
maximized windows. The setting takes effect immediately and is saved
per panel. Transparency remains copy-compatible with the compositor.

How to try: right-click a panel to toggle Panel Options > Show panel edge,
maximize a window and observe the edge and workarea gap.

### Per-window navigation history

Each filer window keeps its own Back and Forward history, including
Up and Forward navigation. History is per filer window, not global, and
survives directory refresh. Use the toolbar Home, Up, Back or Forward
controls, or choose a history entry from the Jump menu.

How to try: open two separate directories, navigate with Up and
Back, then use Forward to return — each window retains its own stack.

### Auto-resize floor for toolbar

Icon view windows auto-size to show at least N toolbar buttons when the
Toolbar button count floor is set in Options. Until the floor is
reached the window grows horizontally before wrapping to a new row.

How to try: set the floor in Options > Tools/Minibuffer >
Toolbar > Auto-resize and open a directory with many items.

### Transparent pinboard label background

Pinboard icon labels can use a transparent background, improving
readability over varied wallpapers while keeping the label text readable.

### Thumbnail blending for JPEG save

JPEG thumbnails with alpha are blended onto the current window background
before saving, avoiding unexpected checker or black backgrounds in saved files.

How to try: browse a directory with translucent thumbnails and save a
thumbnail — the resulting JPEG uses the filer background.

### Opaque theme background by default

New windows default to an opaque theme background. Scrollbar and list
view bleed-through areas are opaque, and list and icon view backgrounds
align. The legacy filer defaulted to a translucent background.

### Default file type colors

The default set of file type colors is optimized for display against the filer
view's default background color and the Adwaita-dark theme background color.

**Note for Fatdog64 users:** If you carry over your current configuration, know
that the Fatdog default file type colors do not constrast well on the new filer
default background color. You may adjust colors in Options > Colours or start
with a new configuration.

### Custom shortcut editor

Menu shortcuts can be edited in a dedicated accelerator editor
Changes apply without editing XML configuration by hand.

How to try: open Options > Menus > Set keyboard shortcuts
and assign a new key to an action.

### Custom SVG toolbar icon theme

The stock toolbar icon set has been replaced with a custom scalable SVG theme
in the ROX-Filer directory. To override any button, place named icons in
your `~/.icons` directory on in `/usr/share/icons/hicolor/scalable/apps`:
* rox-add-new.svg
* rox-bookmarks.svg
* rox-close.svg
* rox-go-home.svg
* rox-go-up.svg
* rox-help.svg
* rox-refresh-view.svg
* rox-save-display.svg
* rox-select.svg
* rox-show-details.svg
* rox-show-dir.svg
* rox-show-hidden.svg
* rox-sort-view.svg
* rox-zoom-auto-fit.svg
* rox-zoom-icon.svg

### Right-click menu App and SendTo submenus

Two new top submenus are always included in the item right-click menu, App...
(for AppDir) and SendTo... These entries are disabled if the current item is not
an AppDir or lacks associated SendTo programs. This new arrangement prevents
excessive menu height when many SendTo programs are associated. Direct access
to an item's SendTo menu is still available via Shift-right-click.

### Right-click menu pre-selection removed

Right-click no longer pre-selects the menu entry under the pointer.
Menus open with no selection, requiring an explicit move to activate.
This matches modern GTK menu behaviour and avoids accidental activation.

### Tooltip border removed

The hard-coded tooltip border stroke has been removed. Tooltips
follow the GTK theme border and padding, reducing visual duplication on
modern themes.

### HiDPI-aware rendering

File icons, emblems and pinboard coordinates are rendered at
physical resolution on HiDPI monitors. Icons scale with the display
scale factor and no longer appear blurry.

### Widget names for CSS customization

ROX-Filer folder windows expose widget names and CSS classes
that can be targeted in `~/.config/gtk-3.0/gtk.css` for
per-application styling. Widget names are set on the toolbar,
icon view, list view, minibuffer, and thumbnail progress bar.
The tooltip popup gains a .rox-tooltip CSS class (custom tooltips
only; GTK3 native tooltips are styled by the theme).

### Instance stylesheet

ROX-Filer loads `style.css` from the user's configuration directory.

Default path: `~/.config/rox.sourceforge.net/ROX-Filer/style.css`

Use `--dump-css` to see the internal CSS as a starting point.
The Colours section of the Options dialog includes a "Reload
stylesheet" button to apply changes without restarting.

See also: [Stylesheet reload via SOAP].

### Stylesheet reload via SOAP

A new SOAP RPC method reloads the external stylesheet without restarting
ROX-Filer. User and theme changes can be applied live. Run the reload
from a script or send the RPC via rox --RPC.

How to try: edit `~/.config/rox.sourceforge.net/ROX-Filer/style.css` then
invoke the reload method or press Reload stylesheet in Options > Colours.

### Keyboard focus indicator

The "Show keyboard focus indicator" option (Options > Filer windows >
New Window) adds a visible rectangle around the focused item when
navigating with the keyboard in both list view and icon view.

If the current GTK theme already provides a focus indicator, this
setting has no effect. If the theme suppresses outlines (as some
themes do), a fallback CSS indicator is injected automatically when
the option is enabled. The option takes effect immediately, with no
restart required.

### Icon view selection

The selection and drag background colors are controlled via CSS named
colors (`@rox_selection_bg`, `@rox_active_bg`). Users can override
these in `~/.config/gtk-3.0/gtk.css`.

```css
@define-color rox_selection_bg #your-color;
@define-color rox_active_bg #your-color;
```

### Rubberband selection

The rubberband selection rectangle is rendered via CSS with
the `.rubberband` CSS class. Fill color, border width,
style, and color are user-configurable via the stylesheet.

### Wink outline

Double-click to open flashes a brief outline around the source item.
The outline is rendered via CSS with the `wink-outline` class using
amber color (`@rox_active_bg`) in both icon and list views. Outline
properties (color, width, style) are user-configurable.

### Minibuffer colors

The minibuffer background and foreground are configurable via
custom styles, following the active GTK theme.

How to try: change the minibuffer colors via the [Instance stylesheet].

---

[This project]: NEWS.md
[jun7]: https://github.com/jun7/ROX-Filer
[Forum]: https://forum.puppylinux.com
[woof-CE]: https://github.com/puppylinux-woof-CE
[Fatdog64 710 Beta]: https://distro.ibiblio.org/fatdog/web/710b.html
[ROX-Filer2]: https://forum.puppylinux.com/viewtopic.php?t=17225

[Unreleased]: #unreleased
[Beta 2 test 2026-10-21]: #beta-2-test-2026-10-21
[Beta 2 test]: #beta-2-test
[Beta 1 test 2026-10-14]: #beta-1-test-2026-10-14
[Beta 1 test]: #beta-1-test
[New since alpha 3]: #new-since-alpha-3
[Changed since alpha 3]: #changed-since-alpha-3
[Fixed since alpha 3]: #fixed-since-alpha-3
[Alpha 3 test 2026-09-15]: #alpha-3-test-2026-09-15
[Alpha 3 test]: #alpha-3-test
[New since alpha 2]: #new-since-alpha-2
[Merged SendTo menus]: #merged-sendto-menus
[Support selecting file names starting with a number]: #support-selecting-file-names-starting-with-a-number
[Show numbered groups in the bookmarks menu]: #show-numbered-groups-in-the-bookmarks-menu
[Improved support for empty panels]: #improved-support-for-empty-panels
[Command-line --options]: #command-line---options
[Command-line --exit]: #command-line---exit
[Fixed since alpha 2]: #fixed-since-alpha-2
[Alpha 2 test 2026-09-09]: #alpha-2-test-2026-09-09
[Alpha 2 test]: #alpha-2-test
[Toolbar opaque under compositor]: #toolbar-opaque-under-compositor
[AppDir menu stock icons]: #appdir-menu-stock-icons
[Keyboard focus after minibuffer]: #keyboard-focus-after-minibuffer
[Export ROX_FILER_CONFIG]: #export-rox-filer-config
[Item menu icons and accelerators]: #item-menu-icons-and-accelerators
[Thumbnailers help]: #thumbnailers-help
[Manage thumbnails removed]: #manage-thumbnails-removed
[Video thumbnails removed]: #video-thumbnails-removed
[Alpha 1 test 2026-09-04]: #alpha-1-test-2026-09-04
[Alpha 1 test]: #alpha-1-test
[Jun7 feature set]: #jun7-feature-set
[Same configuration files]: #same-configuration-files
[Document scope]: #document-scope
[Isolated configuration]: #isolated-configuration
[Sane defaults]: #sane-defaults
[Disable RPC handler]: #disable-rpc-handler
[Select view type option]: #select-view-type-option
[Show panel edge toggle]: #show-panel-edge-toggle
[Per-window navigation history]: #per-window-navigation-history
[Auto-resize floor for toolbar]: #auto-resize-floor-for-toolbar
[Transparent pinboard label background]: #transparent-pinboard-label-background
[Thumbnail blending for JPEG save]: #thumbnail-blending-for-jpeg-save
[Opaque theme background by default]: #opaque-theme-background-by-default
[Default file type colors]: #default-file-type-colors
[Custom shortcut editor]: #custom-shortcut-editor
[Custom SVG icon theme]: #custom-svg-icon-theme
[Right-click menu App and SendTo submenus]: #right-click-menu-app-and-sendto-submenus
[Right-click menu pre-selection removed]: #right-click-menu-pre-selection-removed
[Tooltip border removed]: #tooltip-border-removed
[HiDPI-aware rendering]: #hidpi-aware-rendering
[Widget names for CSS customization]: #widget-names-for-css-customization
[Instance stylesheet]: #instance-stylesheet
[Stylesheet reload via SOAP]: #stylesheet-reload-via-soap
[Keyboard focus indicator]: #keyboard-focus-indicator
[Icon view selection]: #icon-view-selection
[Rubberband selection]: #rubberband-selection
[Wink outline]: #wink-outline
[Minibuffer colors]: #minibuffer-colors
