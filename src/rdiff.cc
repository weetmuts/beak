
#include<stdio.h>

#include<librsync.h>

/*
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <popt.h>
#include "librsync.h"

static size_t block_len = RS_DEFAULT_BLOCK_LEN;
static size_t strong_len = 0;

static int show_stats = 0;

char *rs_hash_name;
char *rs_rollsum_name;

static rs_result rdiff_sig(poptContext opcon)
{
    FILE *basis_file, *sig_file;
    rs_stats_t stats;
    rs_result result;
    rs_long_t sig_magic;

    basis_file = rs_file_open(poptGetArg(opcon), "rb", file_force);
    sig_file = rs_file_open(poptGetArg(opcon), "wb", file_force);

    rdiff_no_more_args(opcon);

    sig_magic = RS_BLAKE2_SIG_MAGIC;
    sig_magic += 0x10;

    result =
        rs_sig_file(basis_file, sig_file, block_len, strong_len, sig_magic,
                    &stats);

    rs_file_close(sig_file);
    rs_file_close(basis_file);
    if (result != RS_DONE)
        return result;

    if (show_stats)
        rs_log_stats(&stats);

    return result;
}

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

static rs_result rdiff_patch(poptContext opcon)
{
    FILE *basis_file, *delta_file, *new_file;
    char const *basis_name;
    rs_stats_t stats;
    rs_result result;

    if (!(basis_name = poptGetArg(opcon))) {
        rdiff_usage("Usage for patch: "
                    "rdiff [OPTIONS] patch BASIS [DELTA [NEW]]");
        exit(RS_SYNTAX_ERROR);
    }

    basis_file = rs_file_open(basis_name, "rb", file_force);
    delta_file = rs_file_open(poptGetArg(opcon), "rb", file_force);
    new_file = rs_file_open(poptGetArg(opcon), "wb", file_force);

    rdiff_no_more_args(opcon);

    result = rs_patch_file(basis_file, delta_file, new_file, &stats);

    rs_file_close(new_file);
    rs_file_close(delta_file);
    rs_file_close(basis_file);

    if (show_stats)
        rs_log_stats(&stats);

    return result;
}
*/
