
#!/bin/bash

# Installs the v3d Okular plugin. The plugin's okularGenerator_poppler.so
# replaces the distribution's generator; the original is preserved in
# <plugindir>/tmp/ so that uninstall.sh can restore it.
#
# Which file is which is detected from the binary itself: a plugin
# build depends on libv3dCommon.so and references the plugin's V3d*
# classes, neither of which a distribution build contains. The backup
# is only taken from a distribution generator, so a plugin build
# already in place is never saved as if it were the original. If the
# saved backup turns out to be a plugin build (an earlier install.sh
# overwrote it) while a distribution generator is in place again (the
# package was reinstalled), the bad backup is replaced with the real
# original.

# A plugin build is recognizable in the binary itself: it depends on
# libv3dCommon.so and references the plugin's V3d* classes, neither
# of which a distribution build of the generator can contain.
is_plugin_build() {
    LC_ALL=C grep -q "libv3dCommon" "$1" 2> /dev/null || \
    LC_ALL=C grep -q "V3dModelManager" "$1" 2> /dev/null
}

PLUGIN_DIR=""
for dir in /usr/lib64/qt6/plugins/okular_generators \
           /usr/lib/x86_64-linux-gnu/qt6/plugins/okular_generators
do
    if [ -d "$dir" ]; then
        PLUGIN_DIR="$dir"
        break
    fi
done

if [ -z "$PLUGIN_DIR" ]; then
    echo "Could not find the Okular generators plugin directory."
    exit 1
fi

POPPLER="$PLUGIN_DIR/okularGenerator_poppler.so"
BACKUP="$PLUGIN_DIR/tmp/okularGenerator_poppler.so"

if [ -e "$POPPLER" ] && ! is_plugin_build "$POPPLER"; then
    # A distribution generator is in place: save it, replacing any
    # saved backup that is actually a plugin build.
    if [ -e "$BACKUP" ] && is_plugin_build "$BACKUP"; then
        echo "Replaced a saved backup that was a plugin build with the distribution's okularGenerator_poppler.so."
    fi
    mkdir -p "$PLUGIN_DIR/tmp"
    mv -f "$POPPLER" "$BACKUP" || \
        echo "Warning: could not back up the distribution's okularGenerator_poppler.so to $PLUGIN_DIR/tmp/"
elif [ -e "$POPPLER" ]; then
    # A previous plugin build is in place; the original, if any, is
    # already saved.
    if [ ! -e "$BACKUP" ] || is_plugin_build "$BACKUP"; then
        echo "Note: the okularGenerator_poppler.so in place is a plugin build and no original is saved. Reinstall the package that provides okularGenerator_poppler.so to restore the distribution's generator."
    fi
else
    # No generator in place.
    if [ -e "$BACKUP" ] && is_plugin_build "$BACKUP"; then
        echo "Note: no okularGenerator_poppler.so in place, and the saved backup is also a plugin build. Reinstall the package that provides okularGenerator_poppler.so to restore the distribution's generator."
    elif [ ! -e "$BACKUP" ]; then
        echo "Note: no distribution okularGenerator_poppler.so found to back up."
    fi
fi

install okularGenerator_poppler.so "$PLUGIN_DIR" || \
    echo "Could not install okularGenerator_poppler.so to $PLUGIN_DIR"

install okularGenerator_v3d.so "$PLUGIN_DIR" || \
    echo "Could not install okularGenerator_v3d.so to $PLUGIN_DIR"

# Install libv3dCommon.so alongside the generator plugins. Both generators
# have RUNPATH=$ORIGIN set at build time, so they resolve this relative to
# their own directory automatically.
install libv3dCommon.so "$PLUGIN_DIR" || \
    echo "Could not install libv3dCommon.so to $PLUGIN_DIR"

install *.glsl "$PLUGIN_DIR" || \
    echo "Could not install the .glsl shaders to $PLUGIN_DIR"

install v3d-mime.xml /usr/share/mime/packages || \
    echo "Could not install v3d-mime.xml to /usr/share/mime/packages"

update-mime-database /usr/share/mime

install okularApplication_v3d.desktop /usr/share/applications/ || \
    echo "Could not install okularApplication_v3d.desktop to /usr/share/applications"

