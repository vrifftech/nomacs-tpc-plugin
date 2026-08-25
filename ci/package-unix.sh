#!/usr/bin/env bash
set -euo pipefail

platform="${1:?usage: package-unix.sh PLATFORM [BUILD_DIR] [OUTPUT_DIR]}"
build_dir="${2:-build}"
output_dir="${3:-dist}"

case "$platform" in
  linux-x86_64|macos-arm64|macos-x86_64) ;;
  *)
    echo "unsupported platform: $platform" >&2
    exit 2
    ;;
esac

plugin="$(find "$build_dir" -type f \( -name 'libqtpc*.so' -o -name 'libqtpc*.dylib' \) ! -path '*/CMakeFiles/*' -print -quit)"
if [[ -z "$plugin" ]]; then
  echo "qtpc plugin binary was not found below $build_dir" >&2
  exit 1
fi

package_name="qt-tpc-image-plugin-${platform}"
stage_parent="${output_dir}/stage"
stage_dir="${stage_parent}/${package_name}"
archive_base="${output_dir}/${package_name}"

rm -rf "$stage_dir"
mkdir -p "$stage_dir/imageformats"
cp "$plugin" "$stage_dir/imageformats/"

for file in README.md LICENSE LICENSE.txt; do
  if [[ -f "$file" ]]; then
    cp "$file" "$stage_dir/"
  fi
done

cat > "$stage_dir/INSTALL.txt" <<EOF
Qt TPC image-format plugin

Platform: ${platform}
Qt build version: ${QT_VERSION:-unknown}
Source commit: ${GITHUB_SHA:-local-build}

Copy the file inside imageformats/ to the imageformats plug-in directory of
the target Qt application, then restart the application.

nomacs locations:
  Linux: application Qt plug-in path/imageformats/
  macOS: nomacs.app/Contents/PlugIns/imageformats/

The Qt major version and CPU architecture must match the target application.
A plug-in built with a newer Qt minor version will not load in an older Qt
runtime. On macOS, changing nomacs.app invalidates its existing code signature;
sign the modified local app bundle again before launching it.
EOF

mkdir -p "$output_dir"
if [[ "$platform" == linux-* ]]; then
  tar -C "$stage_parent" -czf "${archive_base}.tar.gz" "$package_name"
else
  (
    cd "$stage_parent"
    zip -qry "../${package_name}.zip" "$package_name"
  )
fi

rm -rf "$stage_parent"
