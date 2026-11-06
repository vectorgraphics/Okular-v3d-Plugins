
#!/bin/bash

# Uninstalls the v3d Okular plugin.
#
# The plugin's okularGenerator_poppler.so replaces the distribution's
# generator; install.sh preserves the original in <plugindir>/tmp/.
# Which generator is which is detected from the binary itself: a
# plugin build depends on libv3dCommon.so and references the
# plugin's V3d* classes, neither of which a distribution build
# contains. The generator in place is only removed when it is
# recognized as a plugin build, and a saved original is only
# restored when it is recognized as a distribution build.
#
# If both the generator in place and the saved backup are plugin
# builds, the original was lost (an earlier install.sh overwrote the
# backup with a previous plugin build); both plugin builds are
# removed and the user is told to reinstall the package that provides
# the generator.
#
# Known limitation: if the distribution package is removed while the
# plugin is installed, a saved (now stale) original is still
# restored; it cannot load without the package's libraries and is
# replaced if the package is reinstalled.

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

if [ -n "$PLUGIN_DIR" ]; then
    POPPLER="$PLUGIN_DIR/okularGenerator_poppler.so"
    BACKUP="$PLUGIN_DIR/tmp/okularGenerator_poppler.so"

    # These files belong to the plugin whether or not it was installed
    # through install.sh.
    rm -f "$PLUGIN_DIR/okularGenerator_v3d.so" \
          "$PLUGIN_DIR/libv3dCommon.so" \
          "$PLUGIN_DIR"/*.glsl

    if [ -f "$POPPLER" ] && is_plugin_build "$POPPLER"; then
        # The generator in place is the plugin's build.
        if [ -f "$BACKUP" ] && ! is_plugin_build "$BACKUP"; then
            mv -f "$BACKUP" "$POPPLER"
            echo "Removed the plugin's okularGenerator_poppler.so and restored the original."
        elif [ -f "$BACKUP" ]; then
            rm -f "$POPPLER"
            echo "Removed the plugin's okularGenerator_poppler.so, but the saved backup is also a plugin build: the original was lost (an earlier install.sh overwrote the backup). Reinstall the package that provides okularGenerator_poppler.so to restore the distribution's generator."
        else
            rm -f "$POPPLER"
            echo "Removed the plugin's okularGenerator_poppler.so (no original was saved to restore)."
        fi
    elif [ -f "$POPPLER" ]; then
        # The generator in place is a distribution build (or, if the
        # package was updated while the plugin was installed, a newer
        # distribution build than the saved one): leave it in place and
        # discard the stale backup.
        echo "The okularGenerator_poppler.so in place is not a plugin build; leaving it in place."
    elif [ -f "$BACKUP" ] && ! is_plugin_build "$BACKUP"; then
        mv -f "$BACKUP" "$POPPLER"
        echo "Restored the original okularGenerator_poppler.so."
    elif [ -f "$BACKUP" ]; then
        echo "No okularGenerator_poppler.so in place, and the saved backup is also a plugin build: the original was lost (an earlier install.sh overwrote the backup). Reinstall the package that provides okularGenerator_poppler.so to restore the distribution's generator."
    else
        echo "Could not find okularGenerator_poppler.so, was the plugin ever installed?"
    fi
    rm -rf "$PLUGIN_DIR/tmp"
else
    echo "Could not find the Okular generators plugin directory, was the plugin ever installed?"
fi

if [ -f /usr/share/mime/packages/v3d-mime.xml ]; then
    rm -f /usr/share/mime/packages/v3d-mime.xml
else
    echo "Could not find v3d-mime.xml, was the plugin ever installed?"
fi

update-mime-database /usr/share/mime

if [ -f /usr/share/applications/okularApplication_v3d.desktop ]; then
    rm -f /usr/share/applications/okularApplication_v3d.desktop
else
    echo "Could not find okularApplication_v3d.desktop, was the plugin ever installed?"
fi

