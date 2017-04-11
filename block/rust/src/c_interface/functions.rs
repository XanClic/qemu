use c_interface::*;
use libc::c_int;


extern {
    pub fn bdrv_register(bdrv: *mut BlockDriver);
    pub fn error_setg_internal(errp: *mut *mut Error, src: *const u8,
                               line: c_int, func: *const u8,
                               fmt: *const u8, ...);
}
