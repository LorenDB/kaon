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

FILES=$(find src -type f \( -iname "*.cpp" -o -iname "*.h" \))
clang-format -i $FILES
git diff --exit-code
