#!/usr/bin/env bash
# Assemble a self-contained AppImage from the CMake install tree.
set -euo pipefail

version="${1:?usage: build_appimage.sh <version>}"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${root}/build"
appdir="${root}/build/AppDir"
tools_dir="${root}/build/appimage-tools"
output="${root}/Sigil-${version}-linux-x86_64.AppImage"

export APPIMAGE_EXTRACT_AND_RUN=1
export ARCH=x86_64

rm -rf "${appdir}" "${tools_dir}"
mkdir -p "${appdir}" "${tools_dir}"

DESTDIR="${appdir}" cmake --install "${build_dir}" --prefix /usr

qmake_bin="${QMAKE:-$(command -v qmake)}"
export QMAKE="${qmake_bin}"
qt_libexec="$("${qmake_bin}" -query QT_INSTALL_LIBEXECS)"
qt_data="$("${qmake_bin}" -query QT_INSTALL_DATA)"
qt_translations="$("${qmake_bin}" -query QT_INSTALL_TRANSLATIONS)"

install -Dm755 "${qt_libexec}/QtWebEngineProcess" \
    "${appdir}/usr/lib/qt6/libexec/QtWebEngineProcess"
install -d "${appdir}/usr/lib/sigil/resources" "${appdir}/usr/lib/sigil/qtwebengine_locales"
cp -a "${qt_data}/resources/." "${appdir}/usr/lib/sigil/resources/"
cp -a "${qt_translations}/qtwebengine_locales/." "${appdir}/usr/lib/sigil/qtwebengine_locales/"

# Chromium loads NSS at runtime instead of linking it as a normal dependency.
for name in libnss3.so libnssutil3.so libsmime3.so libnspr4.so libplc4.so libplds4.so; do
    lib_path="$(ldconfig -p | awk -v name="${name}" '$1 == name { print $NF; exit }')"
    if [ -n "${lib_path}" ]; then
        cp -L "${lib_path}" "${appdir}/usr/lib/sigil/"
    fi
done
if [ -d /usr/lib/x86_64-linux-gnu/nss ]; then
    install -d "${appdir}/usr/lib/sigil/nss"
    cp -a /usr/lib/x86_64-linux-gnu/nss/. "${appdir}/usr/lib/sigil/nss/"
fi

# Sigil embeds libpython at runtime, so the interpreter home and the modules
# used by its python3lib helpers must travel with the AppImage. The interpreter
# to bundle is the one the build linked against, not the one PATH resolves
# "python3" to - on a CI runner those differ.
py_bin="$(sed -n 's/^Python3_EXECUTABLE:[^=]*=//p' "${build_dir}/CMakeCache.txt" | head -1)"
if [ ! -x "${py_bin}" ]; then
    echo "No usable Python3_EXECUTABLE found in ${build_dir}/CMakeCache.txt" >&2
    exit 1
fi
py_ver="$("${py_bin}" -c 'import sys; print("%d.%d" % sys.version_info[:2])')"
py_stdlib="$("${py_bin}" -c 'import sysconfig; print(sysconfig.get_path("stdlib"))')"
# Debian installs system packages into dist-packages, everything else into
# site-packages, so ask the interpreter which library directories it uses.
py_site_srcs="$("${py_bin}" -c 'import sys; print("\n".join(p for p in sys.path if p.endswith("packages")))')"
py_site="${appdir}/usr/lib/python${py_ver}/site-packages"
if [ ! -d "${py_stdlib}" ]; then
    echo "Python ${py_ver} stdlib directory ${py_stdlib} does not exist" >&2
    exit 1
fi
cp -a "${py_stdlib}" "${appdir}/usr/lib/"
mkdir -p "${py_site}"
for module in lxml css_parser six.py webencodings html5lib dulwich urllib3 certifi chardet cssselect regex; do
    for src in ${py_site_srcs}; do
        if [ -e "${src}/${module}" ]; then
            cp -a "${src}/${module}" "${py_site}/"
            break
        fi
    done
done

libpython="$(ldconfig -p | awk -v name="libpython${py_ver}.so.1.0" '$1 == name { print $NF; exit }')"
if [ -z "${libpython}" ]; then
    echo "libpython${py_ver}.so.1.0 is not available to bundle for ${py_bin}" >&2
    exit 1
fi
cp -L "${libpython}" "${appdir}/usr/lib/"
test -f "${appdir}/usr/lib/libpython${py_ver}.so.1.0"

copy_runtime_deps() {
    local lib target base
    while IFS= read -r lib; do
        base="$(basename "${lib}")"
        case "${base}" in
            libc.so.*|libm.so.*|libpthread.so.*|libdl.so.*|librt.so.*|libresolv.so.*|libutil.so.*|ld-linux*) continue ;;
        esac
        target="${appdir}/usr/lib/sigil/${base}"
        [ -e "${target}" ] || cp -L "${lib}" "${target}"
    done < <(
        find "$@" \( -name '*.so' -o -name '*.so.*' \) -print |
            while IFS= read -r so; do ldd "${so}" 2>/dev/null || true; done |
            awk '$2 == "=>" && $3 ~ /^\// { print $3 }' | sort -u
    )
}
copy_runtime_deps "${appdir}/usr/lib/python${py_ver}" "${py_site}"

install -Dm644 "${root}/COPYING.txt" "${appdir}/usr/share/doc/sigil/COPYING.txt"

desktop_file="${appdir}/usr/share/applications/sigil.desktop"
icon_file="${appdir}/usr/share/pixmaps/sigil.png"
if [ ! -f "${icon_file}" ]; then
    icon_file="${appdir}/usr/share/icons/hicolor/256x256/apps/sigil.png"
fi
test -f "${desktop_file}"
test -f "${icon_file}"

download() {
    curl --fail --location --silent --show-error --retry 3 --output "$1" "$2"
    chmod +x "$1"
}

download "${tools_dir}/linuxdeploy" \
    "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage"
download "${tools_dir}/linuxdeploy-plugin-qt" \
    "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage"

# linuxdeploy resolves NEEDED entries through the file's own rpath and has no
# idea about Sigil's private library directory, so point the rpath at the
# AppDir while packaging. A relocatable rpath is written once linuxdeploy is done.
patchelf --set-rpath "${appdir}/usr/lib/sigil:${appdir}/usr/lib" "${appdir}/usr/lib/sigil/sigil"

# Not --executable: that copies the target into usr/bin first, and the copy is
# skipped when a file of that name is already there. cmake installs a shell
# launcher as usr/bin/sigil, so linuxdeploy would go on to strip and patch the
# script as if it were the ELF it just failed to copy and abort on the parse error.
PATH="${tools_dir}:${PATH}" "${tools_dir}/linuxdeploy" \
    --appdir "${appdir}" \
    --plugin qt \
    --deploy-deps-only "${appdir}/usr/lib/sigil/sigil" \
    --desktop-file "${desktop_file}" \
    --icon-file "${icon_file}"

# linuxdeploy may install its own AppRun, so write ours last.
# It is a symlink into usr/bin unless the qt plugin wrapped it, and writing
# through that symlink would replace the launcher script instead of AppRun.
rm -f "${appdir}/AppRun"
cat > "${appdir}/AppRun" <<'APPRUN'
#!/bin/sh
HERE="$(dirname "$(readlink -f "$0")")"
export SIGIL_EXTRA_ROOT="$HERE/usr/share/sigil"
# Sigil embeds libpython itself; the bundled stdlib keeps that interpreter
# independent of the host. Plugin interpreters get this removed in PluginRunner.
export PYTHONHOME="$HERE/usr"
export QTWEBENGINEPROCESS_PATH="$HERE/usr/lib/qt6/libexec/QtWebEngineProcess"
export QTWEBENGINE_RESOURCES_PATH="$HERE/usr/lib/sigil/resources"
export QTWEBENGINE_LOCALES_PATH="$HERE/usr/lib/sigil/qtwebengine_locales"
export LD_LIBRARY_PATH="$HERE/usr/lib/sigil:$HERE/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
for qtplugins in "$HERE/usr/plugins" "$HERE/usr/lib/qt6/plugins"; do
    if [ -d "$qtplugins" ]; then
        export QT_PLUGIN_PATH="$qtplugins${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}"
        export QT_QPA_PLATFORM_PLUGIN_PATH="$qtplugins/platforms"
        break
    fi
done
exec "$HERE/usr/lib/sigil/sigil" "$@"
APPRUN
chmod +x "${appdir}/AppRun"

patchelf --set-rpath '$ORIGIN:$ORIGIN/..' "${appdir}/usr/lib/sigil/sigil"
patchelf --set-rpath '$ORIGIN/../..' "${appdir}/usr/lib/qt6/libexec/QtWebEngineProcess"

test -f "${appdir}/sigil.desktop"
# linuxdeploy links the icon it prefers for the desktop file's Icon= entry into
# the AppDir root, and it always picks the scalable SVG over the PNG sizes.
# Newer appimagetool builds additionally require a real PNG/XPM for the Icon=
# entry and a valid .DirIcon, so provide both ourselves.
test -e "${appdir}/sigil.svg" || test -e "${appdir}/sigil.png"
if [ ! -f "${appdir}/sigil.png" ]; then
    cp -L "${icon_file}" "${appdir}/sigil.png"
fi
rm -f "${appdir}/.DirIcon"
ln -s sigil.png "${appdir}/.DirIcon"
test -f "${appdir}/usr/lib/sigil/resources/icudtl.dat"

download "${tools_dir}/appimagetool" \
    "https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage"

rm -f "${output}"
"${tools_dir}/appimagetool" "${appdir}" "${output}"
echo "Created ${output}"
