// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
// Copyright (C) 2023 Hendrik
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation version 2.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://gnu.org/licenses/gpl-2.0.txt>

#include "matrix-factory.h"
#include <stdio.h>
#include <string.h>
#include <iostream>

namespace rgb_matrix
{

    MatrixFactory::Options::Options()
        : use_emulator(false)
    {
    }

    RGBMatrixBase* MatrixFactory::CreateMatrix(const MatrixFactory::Options& options)
    {
        RGBMatrixBase *result = nullptr;

        if (options.use_emulator)
        {
            // Create an emulator (nullptr without emulator support)
            result = EmulatorMatrix::Create(options.led_options, options.emulator_options);
        }
        else
        {
            // Create a real hardware matrix
            result = RGBMatrix::CreateFromOptions(options.led_options, options.runtime_options);
        }

        return result;
    }

    bool MatrixFactory::ParseOptionsFromFlags(int *argc, char ***argv,
                                              MatrixFactory::Options *options,
                                              bool remove_consumed_flags)
    {
        if (!options || !argc || !argv || !*argv)
            return false;

        // Handle --led-emulator (and --led-no-emulator) ourselves. Consumed
        // flags are removed by compacting argv, so later parsers and the
        // program never see a leftover placeholder argument.
        char **args = *argv;
        int kept = (*argc > 0) ? 1 : 0;
        for (int i = 1; i < *argc; ++i)
        {
            bool consumed = false;
            if (args[i] && strcmp(args[i], "--led-emulator") == 0)
            {
                options->use_emulator = true;
                consumed = true;
            }
            else if (args[i] && strcmp(args[i], "--led-no-emulator") == 0)
            {
                options->use_emulator = false;
                consumed = true;
            }
            if (!consumed || !remove_consumed_flags)
                args[kept++] = args[i];
        }
        if (remove_consumed_flags)
        {
            for (int i = kept; i < *argc; ++i)
                args[i] = nullptr;
            *argc = kept;
        }

        // Always consume the --led-emulator-* flags so they are accepted (and
        // checked for typos) even when running on hardware or in a build
        // without emulator support.
        if (!rgb_matrix::ParseEmulatorOptionsFromFlags(argc, argv, &options->emulator_options, remove_consumed_flags))
            return false;
        if (options->use_emulator && !EmulatorMatrix::Supported())
        {
            fprintf(stderr, "--led-emulator given, but the rgbmatrix library was built without emulator support (ENABLE_EMULATOR).\n");
            return false;
        }

        // Core --led-* flags apply to hardware and emulator alike.
        return rgb_matrix::ParseOptionsFromFlags(argc, argv, &options->led_options,
                                                 &options->runtime_options,
                                                 remove_consumed_flags);
    }

    void MatrixFactory::PrintMatrixFactoryFlags(FILE *out, const MatrixFactory::Options &defaults)
    {
        fprintf(out, "The following flags are available for RGB matrix configuration:\n");
        if (EmulatorMatrix::Supported())
        {
            fprintf(out, "General matrix selection:\n");
            fprintf(out, "\t--led-emulator          : Use emulator instead of actual hardware (Default: %s)\n\n",
                    defaults.use_emulator ? "on" : "off");
        }
        // Print both sets of options
        fprintf(out, "Hardware matrix options:\n");
        rgb_matrix::PrintMatrixFlags(out, defaults.led_options, defaults.runtime_options);

        if (EmulatorMatrix::Supported())
        {
            fprintf(out, "\nEmulator options:\n");
            rgb_matrix::PrintEmulatorFlags(out, defaults.emulator_options);
        }
    }

} // namespace rgb_matrix