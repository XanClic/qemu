use libc::{c_int,ENOTSUP};
use c_interface::*;


extern fn qcow2_open(_: *mut BlockDriverState, _: *mut QDict, _: c_int,
                     errp: *mut *mut Error)
    -> c_int
{
    error_setg(errp, String::from("Thank you for using Rust"));
    return -ENOTSUP;
}

extern fn qcow2_close(_: *mut BlockDriverState)
{
}


#[no_mangle]
pub extern fn bdrv_qcow2_rust_init()
{
    let mut bdrv = BlockDriver::new("qcow2-rust\0", 0);

    bdrv.bdrv_open = Some(qcow2_open);
    bdrv.bdrv_close = Some(qcow2_close);

    bdrv_register(bdrv);
}
