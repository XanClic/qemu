/*
 * QEMU Error Objects
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.  See
 * the COPYING.LIB file in the top-level directory.
 */

#include "qemu-common.h"
#include "qapi/error.h"
#include "qapi/qmp/qjson.h"
#include "qapi/qmp/qdict.h"
#include "qapi-types.h"
#include "qapi/qmp/qerror.h"

struct Error
{
    char *msg;
    ErrorClass err_class;
};

void error_set_bt(const char *file, const char *func, int line,
                  Error **errp, ErrorClass err_class, const char *fmt, ...)
{
    Error *err;
    char *msg1;
    va_list ap;

    if (errp == NULL) {
        return;
    }
    assert(*errp == NULL);

    err = g_malloc0(sizeof(*err));

    va_start(ap, fmt);
    msg1 = g_strdup_vprintf(fmt, ap);
#ifdef CONFIG_ERROR_BACKTRACE
    err->msg = g_strdup_printf("%s:%i (in %s): %s", file, line, func, msg1);
    g_free(msg1);
#else
    err->msg = msg1;
#endif
    va_end(ap);
    err->err_class = err_class;

    *errp = err;
}

void error_set_errno_bt(const char *file, const char *func, int line,
                        Error **errp, int os_errno, ErrorClass err_class,
                        const char *fmt, ...)
{
    Error *err;
    char *msg1;
    va_list ap;

    if (errp == NULL) {
        return;
    }
    assert(*errp == NULL);

    err = g_malloc0(sizeof(*err));

    va_start(ap, fmt);
    msg1 = g_strdup_vprintf(fmt, ap);
#ifdef CONFIG_ERROR_BACKTRACE
    if (os_errno != 0) {
        err->msg = g_strdup_printf("%s:%i (in %s): %s: %s", file, line, func,
                                   msg1, strerror(os_errno));
    } else {
        err->msg = g_strdup_printf("%s:%i (in %s): %s", file, line, func, msg1);
    }
    g_free(msg1);
#else
    if (os_errno != 0) {
        err->msg = g_strdup_printf("%s: %s", msg1, strerror(os_errno));
        g_free(msg1);
    } else {
        err->msg = msg1;
    }
#endif
    va_end(ap);
    err->err_class = err_class;

    *errp = err;
}

void error_setg_file_open_bt(const char *file, const char *func, int line,
                             Error **errp, int os_errno, const char *filename)
{
    error_set_errno_bt(file, func, line, errp, os_errno,
                       ERROR_CLASS_GENERIC_ERROR, "Could not open '%s'",
                       filename);
}

Error *error_copy(const Error *err)
{
    Error *err_new;

    err_new = g_malloc0(sizeof(*err));
    err_new->msg = g_strdup(err->msg);
    err_new->err_class = err->err_class;

    return err_new;
}

bool error_is_set(Error **errp)
{
    return (errp && *errp);
}

ErrorClass error_get_class(const Error *err)
{
    return err->err_class;
}

const char *error_get_pretty(Error *err)
{
    return err->msg;
}

void error_free(Error *err)
{
    if (err) {
        g_free(err->msg);
        g_free(err);
    }
}


void error_propagate_bt(const char *file, const char *func, int line,
                        Error **dst_err, Error *local_err)
{
    if (local_err) {
#ifdef CONFIG_ERROR_BACKTRACE
        if (dst_err && !*dst_err) {
            Error *err = g_malloc0(sizeof(*err));
            err->msg = g_strdup_printf("%s\n    from %s:%i (in %s)",
                                       local_err->msg, file, line, func);
            err->err_class = local_err->err_class;
            *dst_err = err;
        }
        error_free(local_err);
#else
        if (dst_err && !*dst_err) {
            *dst_err = local_err;
        } else {
            error_free(local_err);
        }
#endif
    }
}
