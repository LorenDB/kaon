#!/usr/bin/env sh

# SPDX-FileCopyrightText: 2023 Nheko Contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later

# taken from https://github.com/Nheko-Reborn/nheko

# Runs the license update
# Return codes:
#  - 1 there are files to be formatted
#  - 0 everything looks fine

set -eu

CPP_FILES=$(find src -type f \( -iname "*.cpp" -o -iname "*.h" \))
clang-format -i $CPP_FILES

QML_FILES=$(find src/qml -type f \( -iname "*.qml" \))
qmlformat6 -i $QML_FILES

git diff --exit-code
