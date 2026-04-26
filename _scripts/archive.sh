#!/bin/bash
#
# archive.sh — Generate Xcode project from CMake and create App Store archive
#
# Usage:
#   ./scripts/archive.sh              # Build signed archive
#   ./scripts/archive.sh +            # Increment build number, then build archive
#   ./scripts/archive.sh + --export   # Bump build, archive, and export .pkg for upload
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_DIR/build-xcode"
ARCHIVE_DIR="$HOME/Library/Developer/Xcode/Archives/$(date +%Y-%m-%d)"
SCHEME="claude-shell"
BUNDLE_ID="com.komsoft.claude-shell"
TEAM_ID="NR5F9UD6RP"
ENTITLEMENTS="$PROJECT_DIR/Resources/ClaudeShell.entitlements"
FLTK_DIR="/Volumes/FastUSB/Sources/fltk-dist/darwin"

# Parse build.info
VERSION=$(grep '^version=' "$PROJECT_DIR/build.info" | cut -d= -f2)
BUILD_NUM=$(grep '^build=' "$PROJECT_DIR/build.info" | cut -d= -f2)

# Parse arguments
DO_EXPORT=false
BUMP_BUILD=false
while [[ $# -gt 0 ]]; do
    case "$1" in
        +)
            BUMP_BUILD=true
            shift
            ;;
        --export)
            DO_EXPORT=true
            shift
            ;;
        *)
            echo "Unknown option: $1"
            echo "Usage: $0 [+] [--export]"
            exit 1
            ;;
    esac
done

# Bump build number if requested
if [ "$BUMP_BUILD" = true ]; then
    BUILD_NUM=$((BUILD_NUM + 1))
    sed -i '' "s/^build=.*/build=$BUILD_NUM/" "$PROJECT_DIR/build.info"
    echo "Build number incremented to $BUILD_NUM"
fi

echo "=== Claude Shell Archive Builder ==="
echo "Version: $VERSION (build $BUILD_NUM)"
echo "Bundle ID: $BUNDLE_ID"
echo ""

# Verify FLTK is available
if [ ! -d "$FLTK_DIR" ]; then
    echo "ERROR: FLTK not found at $FLTK_DIR"
    echo "Mount /Volumes/FastUSB and ensure fltk-dist/darwin exists."
    exit 1
fi

# Find App Store signing identity
echo "--- Checking signing identity ---"
IDENTITY=""
for pattern in "Apple Distribution" "3rd Party Mac Developer Application"; do
    match=$(security find-identity -v -p codesigning | grep "$pattern" | head -1 || true)
    if [ -n "$match" ]; then
        IDENTITY=$(echo "$match" | sed 's/.*"\(.*\)"/\1/')
        break
    fi
done

if [ -z "$IDENTITY" ]; then
    echo "ERROR: No App Store distribution certificate found in Keychain."
    echo ""
    echo "Available identities:"
    security find-identity -v -p codesigning
    echo ""
    echo "You need one of:"
    echo "  - 'Apple Distribution' (modern)"
    echo "  - '3rd Party Mac Developer Application' (legacy)"
    echo "Create at: https://developer.apple.com/account/resources/certificates"
    exit 1
fi

echo "Using: $IDENTITY"
echo ""

# Step 0: Strip quarantine flags from all source files
echo "--- Stripping quarantine attributes ---"
xattr -dr com.apple.quarantine "$PROJECT_DIR" 2>/dev/null || true
echo ""

# Step 1: Generate Xcode project
echo "--- Generating Xcode project ---"
rm -rf "$BUILD_DIR"
cmake -G Xcode -B "$BUILD_DIR" -S "$PROJECT_DIR" \
    -DFLTK_DIR="$FLTK_DIR" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 \
    -DCMAKE_OSX_ARCHITECTURES=arm64
echo ""

# Step 2: Build archive
ARCHIVE_PATH="$ARCHIVE_DIR/ClaudeShell-${VERSION}-${BUILD_NUM}.xcarchive"
mkdir -p "$ARCHIVE_DIR"

echo "--- Building archive ---"
xcodebuild \
    -project "$BUILD_DIR/claude-shell.xcodeproj" \
    -scheme "$SCHEME" \
    -configuration Release \
    -archivePath "$ARCHIVE_PATH" \
    CODE_SIGN_IDENTITY="$IDENTITY" \
    CODE_SIGN_STYLE="Manual" \
    CODE_SIGN_ENTITLEMENTS="$ENTITLEMENTS" \
    DEVELOPMENT_TEAM="$TEAM_ID" \
    ENABLE_HARDENED_RUNTIME=YES \
    archive 2>&1 | tail -5

# Generate dSYM if missing
DSYM_DIR="$ARCHIVE_PATH/dSYMs"
APP_BINARY="$ARCHIVE_PATH/Products/Applications/claude-shell.app/Contents/MacOS/claude-shell"
if [ -z "$(ls -A "$DSYM_DIR" 2>/dev/null)" ] && [ -f "$APP_BINARY" ]; then
    echo "--- Generating dSYM ---"
    dsymutil "$APP_BINARY" -o "$DSYM_DIR/claude-shell.app.dSYM"
fi

echo ""
echo "--- Archive created ---"
echo "Location: $ARCHIVE_PATH"

# Step 3: Export if requested
if [ "$DO_EXPORT" = true ]; then
    EXPORT_DIR="$ARCHIVE_DIR/export"
    EXPORT_PLIST="$SCRIPT_DIR/ExportOptions-AppStore.plist"

    if [ ! -f "$EXPORT_PLIST" ]; then
        echo ""
        echo "Creating ExportOptions-AppStore.plist..."
        cat > "$EXPORT_PLIST" << PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>method</key>
    <string>app-store</string>
    <key>destination</key>
    <string>upload</string>
    <key>signingStyle</key>
    <string>manual</string>
    <key>teamID</key>
    <string>${TEAM_ID}</string>
    <key>signingCertificate</key>
    <string>${IDENTITY}</string>
    <key>provisioningProfiles</key>
    <dict>
        <key>${BUNDLE_ID}</key>
        <string>YOUR_PROVISIONING_PROFILE_NAME</string>
    </dict>
</dict>
</plist>
PLIST
        echo "IMPORTANT: Edit $EXPORT_PLIST and replace YOUR_PROVISIONING_PROFILE_NAME"
        echo "           with your Mac App Store provisioning profile name."
        echo "Then re-run: $0 --export"
        exit 1
    fi

    if grep -q "YOUR_PROVISIONING_PROFILE_NAME" "$EXPORT_PLIST"; then
        echo ""
        echo "ERROR: $EXPORT_PLIST still has placeholder provisioning profile."
        echo "Edit it and replace YOUR_PROVISIONING_PROFILE_NAME with your profile name."
        echo ""
        echo "Installed profiles:"
        ls ~/Library/MobileDevice/Provisioning\ Profiles/ 2>/dev/null || echo "  (none found)"
        echo ""
        echo "To list profile names:"
        echo "  security cms -D -i ~/Library/MobileDevice/Provisioning\\ Profiles/*.provisionprofile 2>/dev/null | grep -A1 Name"
        exit 1
    fi

    echo ""
    echo "--- Exporting for App Store ---"
    xcodebuild -exportArchive \
        -archivePath "$ARCHIVE_PATH" \
        -exportPath "$EXPORT_DIR" \
        -exportOptionsPlist "$EXPORT_PLIST" 2>&1 | tail -10

    echo ""
    echo "--- Export complete ---"
    echo "Location: $EXPORT_DIR"
    echo ""
    echo "To upload to App Store Connect:"
    echo "  xcrun altool --upload-app -f \"$EXPORT_DIR/claude-shell.pkg\" -t macos --apiKey KEY --apiIssuer ISSUER"
    echo "  OR drag into Transporter.app"
fi

echo ""
echo "--- Cleaning up Xcode build dir ---"
rm -rf "$BUILD_DIR"
echo ""
echo "Done."
