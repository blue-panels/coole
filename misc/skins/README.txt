This is an overview of the capabilities of skin files.

See the FAQ for instructions on setting up 256 color or truecolor support in
your terminal.


Header section
==============

    [skin]
        Skin files should define this header section.

        description
            A brief text description of the skin. Currently not used by coole.

        256colors
            Set the value to "true" if the skin uses 256 color extension, but
            does not use truecolor extension. It is a convention to also
            include "256" in the filename.

        truecolors
            Set the value to "true" if the skin uses truecolor extension,
            perhaps along with 256 color extension (but do not set "256colors"
            in that case). It is a convention to also include "16M" in the
            filename.


Color and attribute sections
============================

These sections define the foreground and background colors, and other
graphical attributes to be used in various contexts. See later in this
document for the available color and attribute names.

If a certain keyword is missing from the file, it falls back to the
"_default_" value of the same section. If that is also missing then it further
falls back to the "[core]" section's "_default_".

If a keyword is present but its value does not specify all three of the
foreground color, background color and attributes, then the same fallback rule
applies individually to each of these three properties.

    [core]
        This section describes the elements that are used everywhere.

        _default_
            Default color

        gauge
            Filled part of the progress bar

        input
            Input lines used in query dialogs

        inputmark
            Input selected text (e.g. Shift-Arrows)

        inputunchanged
            Input text before first modification or cursor movement

        disabled
            Disabled UI elements, e.g. in some Options dialogs

        reverse
            Reversed text

        inputhistory
            Button to open the history window of various input fields

        shadow
            Shadow cast by dialogs

        frame
            Frame


    [dialog]
        Non-error dialog windows.

        _default_

        dfocus
            Active element (in focus)

        dhotnormal
            Hotkeys

        dhotfocus
            Hotkey of the active element

        dselnormal
            Selected items in non-focused dropdown lists, e.g. in Find File
            results after Tab'bing away

        dselfocus
            Selected item in focused dropdown lists, e.g. in Find File results
            or skin selector

        dtitle
            Title

        dframe
            Frame


    [error]
        Error dialog windows.

        _default_
        errdfocus
        errdhotnormal
        errdhotfocus
        errdtitle
        errdframe
            Like their counterpart in "[dialog]"


    [menu]
        Dropdown menu (F9).

        _default_
            Inactive items

        menusel
            Active item (in focus)

        menuhot
            Hotkeys

        menuhotsel
            Hotkey of the active item

        menuinactive
            The menubar when the menu is not invoked

        menuframe
            Frame


    [popupmenu]
        Popup menu (user menu: F11; also Alt-E for codepage).

        _default_
            Inactive items

        menusel
            Active item (in focus)

        menutitle
            Title

        menuframe
            Frame


    [buttonbar]
        The bottom row.

        hotkey
            Function key number

        button
            Action's brief label


    [statusbar]
        Editor's top bar

        _default_
            Default color


    [help]
        Help window (F1).

        _default_
            Default color

        helpbold
            Element originally intended to be displayed in bold

        helpitalic
            Element originally intended to be displayed in italic

        helplink
            Links

        helpslink
            Active link (on focus)

        helpframe
            Frame


    [editor]
        The editor.

        _default_
            Default color

        editbold
            Element with bold attribute (e.g. found text)

        editmarked
            Selected text

        editwhitespace
            Tabs and trailing spaces

        editnonprintable
            Non-printable characters

        editrightmargin
            Right margin

        editlinestate
            Line state area

        editbg
            Area not covered by any editor window (in multi-window mode)

        editframe
            Frame of non-active editor window (in multi-window mode)

        editframeactive
            Frame of active editor window (in multi-window mode)

        editframedrag
            Frame of editor window being moved or resized (in multi-window
            mode)

        bookmark
            Lines bookmarked manually

        bookmarkfound
            Lines as the result of "Find all"

        breakpoint
            Mark of a breakpoint in the gutter; unset, bright red on the
            background of the gutter

        breakpointpending
            Mark of a breakpoint GDB has not taken yet, or has refused

        breakpointdisabled
            Mark of a disabled breakpoint

        breakpointline
            Lines with a breakpoint; unset, they are not colored

        execmark
            Mark of the line where the debugged program stopped; unset,
            yellow on the background of the gutter

        execline
            The line where the debugged program stopped; unset, the
            color of bookmarkfound

        editnote
            Notes of a plugin after the text of a line: the values of the
            variables of the debugger; unset, the color of
            editwhitespace

        builderror
        buildwarning
            Marks of a line the compiler has named an error or a
            warning of


    [lines]
        Frame characters used all throughout coole.

        horiz
        vert
        lefttop
        righttop
        leftbottom
        rightbottom
        topmiddle
        bottommiddle
        leftmiddle
        rightmiddle
        cross
            Light frames, or inner lines of frames. Typically represented by
            single lines.

        dhoriz
        dvert
        dlefttop
        drighttop
        dleftbottom
        drightbottom
        dtopmiddle
        dbottommiddle
        dleftmiddle
        drightmiddle
            Heavy frames, used for major boxes. Often identical to light
            frames, but often double lines are used. For "d*middle", the short
            stem is supposed to be light (matching "horiz", "vert" etc.).


    [scrollbar]
        The scrollbars of the windows, the lists and the tables: an arrow at
        each end, a track between them and a thumb in the track.

        vstart
        vend
            The arrows at the top and at the bottom of a vertical bar.
            Default: ^ and v

        vtrack
            The fill of the track of a vertical bar. Default: vert of [lines]

        vthumb
            The thumb of a vertical bar. Default: a full block, '#' where the
            terminal has not the character

        vthumblen
            The cells of the thumb of a vertical bar when it fits; else it is
            half the track. Default: 6

        hstart
        hend
        htrack
        hthumb
        hthumblen
            The same of a horizontal bar. Default: < and >, horiz of [lines],
            a black square, which sits in the middle of the line, and 4


    [widget-editor]
        Symbols managing multple windows of the editor

        window-state-char
            Maximize or unmaximize an editor window

        window-close-char
            Close an editor window

        fold-open-char
            Indicator for an open (expanded) fold in the editor

        fold-close-char
            Indicator for a closed (collapsed) fold in the editor

        breakpoint-char
        breakpoint-pending-char
        breakpoint-disabled-char
            Marks of a breakpoint in the gutter: taken by GDB, not taken
            yet or refused, disabled (default: a filled circle, a lozenge
            and an empty circle; o ? - on a terminal that is not UTF-8)

        exec-char
        exec-breakpoint-char
            Mark of the line where the debugged program stopped, and of
            that line when it has a breakpoint (default: > and a diamond;
            > @ on a terminal that is not UTF-8)

        tree-closed-char
        tree-open-char
            Marks of a closed and an open directory in the tree of the
            project (default: > and a triangle; + and - on a terminal that
            is not UTF-8)

        build-error-char
        build-warning-char
            Marks of a line the compiler has named an error or a warning
            of (default: a multiplication sign and !; E and ! on a
            terminal that is not UTF-8)


Git graph section
=================

    [git-graph]
        Characters of the git plugin: one character a key

        ahead
        behind
            How far the branch is from its upstream, in the title of the
            window Git: the commits it has that the upstream has not, and
            the other way (default: + and -; the skins of coole have arrows
            up and down)


Aliases section
===============

    [aliases]
        This optional section can define aliases for single colors (not color
        pairs), or for combination of attributes; in other words, for
        semicolon-separated fragments of parameters.

        This can make it easier to develop skins, or a set of similar skins,
        that reuse the same color in multiple contexts.

        Aliases can refer to other aliases as long as they don't form a loop.

        Example:
            [aliases]
                myfavfg = green
                myfavbg = black
                myfavattr = bold+italic
            [core]
                _default_ = myfavfg;myfavbg;myfavattr


Color pair and attribute definitions
====================================

Keywords defining the colors and attributes have the following syntax:

    keyword = foreground_color;background_color;attributes

Omitted values are each subjected to the fallback rules described above.

Example:
    [core]
        # green text on blue background
        _default_ = green;blue

        # green (default) text on black background
        input = ;black

        # yellow text on blue (default) background
        gauge = yellow

        # yellow text on blue (default) background, underlined
        disabled = yellow;;underline


Default colors
--------------

The color name "default" refers to the terminal's default colors. That is, if
used in foreground color position then it refers to the terminal's default
foreground color, and if used in background color position then it's the
terminal's default background.

This is mostly useful for creating a transparent skin where the terminal's
(or desktop's) background color or background picture remains visible.


Legacy 16 color mode
--------------------

If you aim for the highest compatibility with terminals, you should restrict
your skin to the standard 16 colors (in addition to the terminal's defaults).
These colors each have two names: the preferred human-friendly one, and a
technical one referring to their color index.

        black = color0             gray = color8
          red = color1        brightred = color9
        green = color2      brightgreen = color10
        brown = color3           yellow = color11
         blue = color4       brightblue = color12
      magenta = color5    brightmagenta = color13
         cyan = color6       brightcyan = color14
    lightgray = color7            white = color15

With 8 color terminal settings (such as TERM=linux or TERM=xterm), bright
foreground colors (second column) are emulated in coole by enabling the bold
attribute, and the colors are mapped to their darker counterpart (first
column). In turn, the terminal may (or may not) interpret the bold flag as
brighter foreground color, thus reverting to the originally specified bright
foreground.

Even in terminals that support more colors (such as TERM=xterm-256color), the
first 8 foreground colors combined with the bold attribute might be converted
to their brighter counterpart. This depends on choice of the terminal or a
setting thereof, coole having no control over it.

Plus, the exact color values heavily depend on the user's chosen palette.

For all these reasons, a skin using these colors can look quite different
across systems.


256 color extension
-------------------

Almost all graphical terminal emulators support the 256 color extension.

By adding "256colors = true" to the "[skin]" section, you get access to 240
additional colors (a 6x6x6 color cube and a 24 color grayscale ramp) on top of
the standard 16 and the default colors.

Colors of the 6x6x6 cube are called "rgb000" to "rgb555", all three digits
ranging from 0 to 5, corresponding to the R, G and B color components.
Alternatively, you can use their technical color index: "color16" to
"color231".

Colors of the grayscale ramp are "gray0" to "gray23", or "color232" to
"color255".

There are scripts available that display these colors in the terminal (e.g.
"256colors2.pl" shipped in Xterm's source code), as well as web pages showing
them (e.g. https://www.ditig.com/256-colors-cheat-sheet).


Truecolor extension
-------------------

A wide range of terminals support the truecolor extension.

By adding "truecolors = true" to the "[skin]" section (instead of the
"256colors" keyword), you get access to 2^24 (about 16 million) direct colors
on top of the 256 palette and the default ones.

Truecolors are referred to by the standard "#rrggbb" or "#rgb" notation. The
short form is interpreted by repeating each hex digit, e.g. "#5AF" is a
shorthand for "#55AAFF".


Attributes
----------

The third value can specify special attributes to apply. Supported attributes
are:

    bold
    underline
    italic
    reverse
    blink

Append more with a plus sign, e.g. "bold+italic".

Omitting the field makes it use the fallback value as described above. Use any
unrecognized word (e.g. "none" or "default") to prevent fallback and disable
all attributes.

Support for each attribute is subject to the underlying terminal. "bold", if
combined with the first 8 palette colors, often converts those colors to their
brighter counterpart. "reverse" has a wide range of interpretations across
terminals, the only case you should use it is when a "default" color is
involved, otherwise you can just directly set the desired colors.

