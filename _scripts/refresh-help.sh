#!/bin/bash
#
# refresh-help.sh — Force macOS Help Viewer to drop its cached copy
# of the Claude Shell help book and re-index from the freshly built
# bundle.
#
# Why: helpd registers help books by CFBundleHelpBookName+bundle id
# and keeps the indexed content cached even after the .app's HelpDocs
# folder is rewritten. Bumping CFBundleVersion is not enough.
#
# Usage:
#   ./_scripts/refresh-help.sh           # use ./build/ClaudeShell.app
#   ./_scripts/refresh-help.sh /path/to/ClaudeShell.app
#

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
APP_PATH="${1:-$PROJECT_DIR/build/ClaudeShell.app}"

if [ ! -d "$APP_PATH" ]; then
    echo "error: app bundle not found: $APP_PATH" >&2
    exit 1
fi

echo "Refreshing Help Viewer cache for: $APP_PATH"

# 1. Close any open Help Viewer windows so the cache can be released.
osascript -e 'tell application "Help Viewer" to quit' 2>/dev/null || true

# 2. Stop helpd (it auto-restarts on next request).
killall -9 helpd 2>/dev/null || true
killall -9 "Help Viewer" 2>/dev/null || true

# 3. Wipe per-user help caches. The Group Container cache is the
#    one most people miss — Help Viewer copies the help book there
#    keyed by <bundle-id>.<book-name>*<short-version>.help/, and that
#    key does not change between builds (CFBundleShortVersionString
#    is "1.1"), so the cached HTML is reused indefinitely.
BUNDLE_ID=$(/usr/libexec/PlistBuddy -c "Print :CFBundleIdentifier" \
    "$APP_PATH/Contents/Info.plist" 2>/dev/null || echo "")
rm -rf "$HOME/Library/Caches/com.apple.help"*
rm -rf "$HOME/Library/Caches/com.apple.helpd"
rm -rf "$HOME/Library/Caches/com.apple.helpviewer"*
rm -f  "$HOME/Library/Preferences/com.apple.helpd.plist"
rm -rf "$HOME/Library/HTTPStorages/com.apple.helpd"
if [ -n "$BUNDLE_ID" ]; then
    rm -rf "$HOME/Library/Group Containers/group.com.apple.helpviewer.content/Library/Caches/$BUNDLE_ID."*.help
fi

# 4. Touch the bundle so LaunchServices re-reads it on next register.
touch "$APP_PATH"

# 5. Re-register the bundle with LaunchServices so helpd discovers
#    the updated CFBundleHelpBookName mapping. Also unregister any
#    stale paths sharing this bundle id (e.g. Xcode DerivedData
#    archive copies) — helpd may otherwise resolve the help book to
#    one of those instead of the live build.
LSREG="/System/Library/Frameworks/CoreServices.framework/Versions/A/Frameworks/LaunchServices.framework/Versions/A/Support/lsregister"
if [ -x "$LSREG" ]; then
    BUNDLE_ID=$(/usr/libexec/PlistBuddy -c "Print :CFBundleIdentifier" \
        "$APP_PATH/Contents/Info.plist" 2>/dev/null || echo "")
    if [ -n "$BUNDLE_ID" ]; then
        "$LSREG" -dump 2>/dev/null \
            | awk '/^path:/{p=$2} /^identifier:/{if($2=="'"$BUNDLE_ID"'") print p}' \
            | while read -r STALE; do
                if [ -n "$STALE" ] && [ "$STALE" != "$APP_PATH" ] && [ ! -d "$STALE" ]; then
                    echo "  unregistering stale: $STALE"
                    "$LSREG" -u "$STALE" 2>/dev/null || true
                fi
            done
    fi
    "$LSREG" -f "$APP_PATH"
fi

# 6. Rebuild the help index inside the bundle (safe to re-run).
HELP_DIR="$APP_PATH/Contents/Resources/English.lproj/HelpDocs"
if [ -d "$HELP_DIR" ]; then
    hiutil -Caf "$HELP_DIR/HelpDocs.helpindex" "$HELP_DIR"
fi

echo "Done. Open Help from the app's Help menu (or run the app once first)."
