/*
 Copyright (C) 2020 Fredrik Öhrström

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include"always.h"
#include"log.h"
#include"rdiff.h"
#include"util.h"

#include<librsync.h>

static size_t block_len = RS_DEFAULT_BLOCK_LEN;
static size_t strong_len = 0;

bool generateSignature(Path *old, FileSystem *old_fs,
                       Path *sig, FileSystem *sig_fs)
{
    rs_stats_t stats;

    FILE *oldf = old_fs->openAsFILE(old, "rb");
    FILE *sigf = sig_fs->openAsFILE(sig, "rwb");
    rs_result rc = rs_sig_file(oldf, sigf, block_len, strong_len, &stats);

    fclose(sigf);
    fclose(oldf);

    if (rc != RS_DONE) return false;

    rs_log_stats(&stats);

    return true;
}

bool generateDelta(Path *sig, FileSystem *sig_fs,
                   Path *target, FileSystem *target_fs,
                   Path *delta, FileSystem *delta_fs)
{
    rs_stats_t stats;
    rs_signature_t *sumset;

    FILE *sigf = sig_fs->openAsFILE(sig, "rb");
    FILE *targetf = target_fs->openAsFILE(target, "rb");
    FILE *deltaf = delta_fs->openAsFILE(delta, "rb");

    rs_result rc = rs_loadsig_file(sigf, &sumset, &stats);
    if (rc != RS_DONE) goto err;

    rs_log_stats(&stats);

    rc = rs_build_hash_table(sumset);
    if (rc != RS_DONE) goto err;

    rc = rs_delta_file(sumset, targetf, deltaf, &stats);
    if (rc != RS_DONE) goto err;

    rs_log_stats(&stats);

    fclose(deltaf);
    fclose(targetf);
    fclose(sigf);

    return true;

err:

    fclose(deltaf);
    fclose(targetf);
    fclose(sigf);

    return false;
}

bool applyPatch(Path *old, FileSystem *old_fs,
                Path *delta, FileSystem *delta_fs,
                Path *target, FileSystem *target_fs)
{
    rs_stats_t stats;

    FILE *oldf = old_fs->openAsFILE(old, "rb");
    FILE *deltaf = delta_fs->openAsFILE(delta, "rb");
    FILE *targetf = target_fs->openAsFILE(target, "rwb");

    rs_result rc = rs_patch_file(oldf, deltaf, targetf, &stats);

    fclose(oldf);
    fclose(deltaf);
    fclose(targetf);

    if (rc != RS_DONE) return false;

    rs_log_stats(&stats);

    return true;
}

/*
static rs_result rdiff_delta(poptContext opcon)
{
    FILE *sig_file, *new_file, *delta_file;
    char const *sig_name;
    rs_result result;
    rs_signature_t *sumset;
    rs_stats_t stats;

    if (!(sig_name = poptGetArg(opcon))) {
        rdiff_usage("Usage for delta: "
                    "rdiff [OPTIONS] delta SIGNATURE [NEWFILE [DELTA]]");
        exit(RS_SYNTAX_ERROR);
    }

    sig_file = rs_file_open(sig_name, "rb", file_force);
    new_file = rs_file_open(poptGetArg(opcon), "rb", file_force);
    delta_file = rs_file_open(poptGetArg(opcon), "wb", file_force);

    rdiff_no_more_args(opcon);

    result = rs_loadsig_file(sig_file, &sumset, &stats);
    if (result != RS_DONE)
        return result;

    if (show_stats)
        rs_log_stats(&stats);

    if ((result = rs_build_hash_table(sumset)) != RS_DONE)
        return result;

    result = rs_delta_file(sumset, new_file, delta_file, &stats);

    rs_file_close(delta_file);
    rs_file_close(new_file);
    rs_file_close(sig_file);

    if (show_stats) {
        rs_signature_log_stats(sumset);
        rs_log_stats(&stats);
    }

    rs_free_sumset(sumset);

    return result;
}
*/
