use libc::{c_int,c_ulong,c_void,size_t};
use std::ptr;

mod functions;


#[repr(C)]
pub struct BlockDriver {
    pub format_name: *const u8,
    pub instance_size: c_int,

    pub is_filter: bool,

    pub bdrv_recurse_is_first_non_filter: Option<
            extern fn(bs: *mut BlockDriverState,
                      candidate: *mut BlockDriverState)
                -> bool
        >,

    pub bdrv_probe: Option<
            extern fn(buf: *const u8, buf_size: c_int, filename: *const u8)
                -> c_int
        >,
    pub bdrv_probe_device: Option<extern fn(filename: *const u8) -> c_int>,

    pub bdrv_parse_filename: Option<
            extern fn(filename: *const u8,
                      options: *mut QDict,
                      errp: *mut *mut Error)
        >,
    pub bdrv_needs_filename: bool,

    pub supports_backing: bool,

    pub bdrv_reopen_prepare: Option<
            extern fn(reopen_state: *mut BDRVReopenState,
                      queue: *mut BlockReopenQueue,
                      errp: *mut *mut Error)
                -> c_int
        >,
    pub bdrv_reopen_commit: Option<
            extern fn(reopen_state: *mut BDRVReopenState)
        >,
    pub bdrv_reopen_abort: Option<
            extern fn(reopen_state: *mut BDRVReopenState)
        >,
    pub bdrv_join_options: Option<
            extern fn(options: *mut QDict, old_options: *mut QDict)
        >,

    pub bdrv_open: Option<
            extern fn(bs: *mut BlockDriverState, options: *mut QDict,
                      flags: c_int, errp: *mut *mut Error)
                -> c_int
        >,
    pub bdrv_file_open: Option<
            extern fn(bs: *mut BlockDriverState, options: *mut QDict,
                      flags: c_int, errp: *mut *mut Error)
                -> c_int
        >,
    pub bdrv_close: Option<extern fn(bs: *mut BlockDriverState)>,
    pub bdrv_create: Option<
            extern fn(filename: *const u8, opts: *mut QemuOpts,
                      errp: *mut *mut Error)
                -> c_int
        >,
    pub bdrv_set_key: Option<DeprecatedFn>,
    pub bdrv_make_empty: Option<extern fn(bs: *mut BlockDriverState)>,

    pub bdrv_refresh_filename: Option<
            extern fn(bs: *mut BlockDriverState, options: *mut QDict)
        >,

    pub bdrv_aio_readv: Option<DeprecatedFn>,
    pub bdrv_aio_writev: Option<DeprecatedFn>,
    pub bdrv_aio_flush: Option<DeprecatedFn>,
    pub bdrv_aio_pdiscard: Option<DeprecatedFn>,

    pub bdrv_co_readv: Option<DeprecatedFn>,
    pub bdrv_co_preadv: Option<
            extern fn(bs: *mut BlockDriverState, offset: u64, bytes: u64,
                      qiov: *mut QEMUIOVector, flags: c_int)
                -> c_int
        >,
    pub bdrv_co_writev: Option<DeprecatedFn>,
    pub bdrv_co_writev_flags: Option<DeprecatedFn>,
    pub bdrv_co_pwritev: Option<
            extern fn(bs: *mut BlockDriverState, offset: u64, bytes: u64,
                      qiov: *mut QEMUIOVector, flags: c_int)
                -> c_int
        >,

    pub bdrv_co_pwrite_zeroes: Option<
            extern fn(bs: *mut BlockDriverState, offset: i64, count: c_int,
                      flags: c_int /* BdrvRequestFlags */)
                -> c_int
        >,
    pub bdrv_co_pdiscard: Option<
            extern fn(bs: *mut BlockDriverState, offset: i64, count: c_int)
                -> c_int
        >,
    pub bdrv_co_get_block_status: Option<
            extern fn(bs: *mut BlockDriverState, sector_num: i64,
                      nb_sectors: c_int, pnum: *mut c_int,
                      file: *mut *mut BlockDriverState)
                -> i64
        >,

    pub bdrv_invalidate_cache: Option<
            extern fn(bs: *mut BlockDriverState, errp: *mut *mut Error)
        >,
    pub bdrv_inactivate: Option<extern fn(bs: *mut BlockDriverState) -> c_int>,

    pub bdrv_co_flush: Option<extern fn(bs: *mut BlockDriverState) -> c_int>,

    pub bdrv_co_flush_to_disk: Option<
            extern fn(bs: *mut BlockDriverState) -> c_int
        >,

    pub bdrv_co_flush_to_os: Option<
            extern fn(bs: *mut BlockDriverState) -> c_int
        >,

    pub protocol_name: *const u8,
    pub bdrv_truncate: Option<
            extern fn(bs: *mut BlockDriverState, offset: i64,
                      errp: *mut *mut Error)
                -> c_int
        >,

    pub bdrv_getlength: Option<extern fn(bs: *mut BlockDriverState) -> c_int>,
    pub has_variable_length: bool,
    pub bdrv_get_allocated_file_size: Option<
            extern fn(bs: *mut BlockDriverState) -> i64
        >,

    pub bdrv_co_pwritev_compressed: Option<
            extern fn(bs: *mut BlockDriverState, offset: u64, bytes: u64,
                      qiov: *mut QEMUIOVector)
                -> c_int
        >,

    pub bdrv_snapshot_create: Option<
            extern fn(bs: *mut BlockDriverState, sn_info: *mut QEMUSnapshotInfo)
                -> c_int
        >,
    pub bdrv_snapshot_goto: Option<
            extern fn(bs: *mut BlockDriverState, snapshot_id: *const u8)
                -> c_int
        >,
    pub bdrv_snapshot_delete: Option<
            extern fn(bs: *mut BlockDriverState, snapshot_id: *const u8,
                      name: *const u8, errp: *mut *mut Error)
                -> c_int
        >,
    pub bdrv_snapshot_list: Option<
            extern fn(bs: *mut BlockDriverState,
                      psn_info: *mut *mut QEMUSnapshotInfo)
                -> c_int
        >,
    pub bdrv_snapshot_load_tmp: Option<
            extern fn(bs: *mut BlockDriverState, snapshot_id: *const u8,
                      name: *const u8, errp: *mut *mut Error)
                -> c_int
        >,

    pub bdrv_get_info: Option<
            extern fn(bs: *mut BlockDriverState, bdi: *mut BlockDriverInfo)
                -> c_int
        >,
    /* Note that the return object should be allocated in the C program */
    pub bdrv_get_specific_info: Option<
            extern fn(bs: *mut BlockDriverState) -> *mut ImageInfoSpecific
        >,

    pub bdrv_save_vmstate: Option<
            extern fn(bs: *mut BlockDriverState, qiov: *mut QEMUIOVector,
                      pos: i64)
                -> c_int
        >,
    pub bdrv_load_vmstate: Option<
            extern fn(bs: *mut BlockDriverState, qiov: *mut QEMUIOVector,
                      pos: i64)
                -> c_int
        >,

    pub bdrv_change_backing_file: Option<
            extern fn(bs: *mut BlockDriverState,
                      backing_file: *const u8, backing_fmt: *const u8)
                -> c_int
        >,

    pub bdrv_is_inserted: Option<extern fn(bs: *mut BlockDriverState) -> bool>,
    pub bdrv_media_changed: Option<
            extern fn(bs: *mut BlockDriverState) -> c_int
        >,
    pub bdrv_eject: Option<
            extern fn(bs: *mut BlockDriverState, eject_flag: bool)
        >,
    pub bdrv_lock_medium: Option<
            extern fn(bs: *mut BlockDriverState, locked: bool)
        >,

    pub bdrv_aio_ioctl: Option<DeprecatedFn>,
    pub bdrv_co_ioctl: Option<
            extern fn(bs: *mut BlockDriverState, req: c_ulong, buf: *mut c_void)
                -> c_int
        >,

    pub create_opts: *mut QemuOptsList,

    pub bdrv_check: Option<
            extern fn(bs: *mut BlockDriverState, result: *mut BdrvCheckResult,
                      fix: c_int /* BdrvCheckResult */)
                -> c_int
        >,

    pub bdrv_amend_options: Option<
            extern fn(bs: *mut BlockDriverState, opts: *mut QemuOpts,
                      status_cb: BlockDriverAmendStatusCB,
                      cb_opaque: *mut c_void)
                -> c_int
        >,

    pub bdrv_debug_event: Option<
            extern fn(bs: *mut BlockDriverState,
                      event: c_int /* BlkdebugEvent */)
        >,

    pub bdrv_debug_breakpoint: Option<
            extern fn(bs: *mut BlockDriverState, event: *const u8,
                      tag: *const u8)
                -> c_int
        >,
    pub bdrv_debug_remove_breakpoint: Option<
            extern fn(bs: *mut BlockDriverState, tag: *const u8) -> c_int
        >,
    pub bdrv_debug_resume: Option<
            extern fn(bs: *mut BlockDriverState, tag: *const u8) -> c_int
        >,
    pub bdrv_debug_is_suspended: Option<
            extern fn(bs: *mut BlockDriverState, tag: *const u8) -> bool
        >,

    pub bdrv_refresh_limits: Option<
            extern fn(bs: *mut BlockDriverState, errp: *mut *mut Error)
        >,

    pub bdrv_has_zero_init: Option<
            extern fn(bs: *mut BlockDriverState) -> c_int
        >,

    pub bdrv_detach_aio_context: Option<extern fn(bs: *mut BlockDriverState)>,

    pub bdrv_attach_aio_context: Option<
            extern fn(bs: *mut BlockDriverState, new_context: *mut AioContext)
        >,

    pub bdrv_io_plug: Option<extern fn(bs: *mut BlockDriverState)>,
    pub bdrv_io_unplug: Option<extern fn(bs: *mut BlockDriverState)>,

    pub bdrv_probe_blocksizes: Option<
            extern fn(bs: *mut BlockDriverState, bsz: *mut BlockSizes)
                -> c_int
        >,
    pub bdrv_probe_geometry: Option<
            extern fn(bs: *mut BlockDriverState, geo: *mut HDGeometry)
                -> c_int
        >,

    pub bdrv_drain: Option<extern fn(bs: *mut BlockDriverState)>,

    pub bdrv_add_child: Option<
            extern fn(parent: *mut BlockDriverState,
                      child: *mut BlockDriverState,
                      errp: *mut *mut Error)
        >,
    pub bdrv_del_child: Option<
            extern fn(parent: *mut BlockDriverState,
                      child: *mut BlockDriverState,
                      errp: *mut *mut Error)
        >,

    pub bdrv_check_perm: Option<
            extern fn(bs: *mut BlockDriverState, perm: u64, shared: u64,
                      errp: *mut *mut Error)
                -> c_int
        >,
    pub bdrv_set_perm: Option<
            extern fn(bs: *mut BlockDriverState, perm: u64, shared: u64)
        >,
    pub bdrv_abort_perm_update: Option<extern fn(bs: *mut BlockDriverState)>,

    pub bdrv_child_perm: Option<
            extern fn(bs: *mut BlockDriverState, c: *mut BdrvChild,
                      role: *const BdrvChildRole,
                      parent_perm: u64, parent_shared: u64,
                      nperm: *mut u64, nshared: *mut u64)
        >,

    pub list: BlockDriverListEntry,
}

#[repr(C)]
pub struct BlockDriverState {
    pub open_flags: c_int,
    pub read_only: bool,
    pub encrypted: bool,
    pub valid_key: bool,
    pub sg: bool,
    pub probed: bool,

    pub drv: *mut BlockDriver,
    pub opaque: *mut c_void,

    pub aio_context: *mut AioContext,
}

#[repr(C)]
pub struct BDRVReopenState {
    pub bs: *mut BlockDriverState,
    pub flags: c_int,
    pub options: *mut QDict,
    pub explicit_options: *mut QDict,
    pub opaque: *mut c_void,
}

#[repr(C)]
pub struct BlockDriverListEntry {
    le_next: *mut BlockDriver,
    le_prev: *mut *mut BlockDriver,
}

#[repr(C)]
pub struct BdrvCheckResult {
    pub corruptions: c_int,
    pub leaks: c_int,
    pub check_errors: c_int,
    pub corruptions_fixed: c_int,
    pub leaks_fixed: c_int,
    pub image_end_offset: i64,
    pub bfi: c_int, /* BlockFragInfo */
}

#[repr(C)]
pub struct BlockSizes {
    pub phys: u32,
    pub log: u32,
}

#[repr(C)]
pub struct HDGeometry {
    pub heads: u32,
    pub sectors: u32,
    pub cylinders: u32,
}

#[repr(C)]
pub struct BdrvChild {
    pub bs: *mut BlockDriverState,
    pub name: *mut u8,
    pub role: *const BdrvChildRole,
    pub opaque: *mut c_void,

    pub perm: u64,

    pub shared_perm: u64,

    pub next: BdrvChildListEntry,
    pub next_parent: BdrvChildListEntry,
}

#[repr(C)]
pub struct BdrvChildListEntry {
    pub le_next: *mut BdrvChild,
    pub le_prev: *mut *mut BdrvChild,
}

#[repr(C)]
pub struct BdrvChildRole {
    pub stay_at_node: bool,

    pub inherit_options: Option<
            extern fn(child_flags: *mut c_int, child_options: *mut QDict,
                      parent_flags: c_int, parent_options: *mut QDict)
        >,

    pub change_media: Option<extern fn(child: *mut BdrvChild, load: bool)>,
    pub resize: Option<extern fn(child: *mut BdrvChild)>,

    pub get_name: Option<extern fn(child: *mut BdrvChild) -> *const u8>,

    /* Return value should probably be allocated in the C program */
    pub get_parent_desc: Option<extern fn(child: *mut BdrvChild) -> *mut u8>,

    pub drained_begin: Option<extern fn(child: *mut BdrvChild)>,
    pub drained_end: Option<extern fn(child: *mut BdrvChild)>,

    pub attach: Option<extern fn(child: *mut BdrvChild)>,
    pub detach: Option<extern fn(child: *mut BdrvChild)>,
}

#[repr(C)]
pub struct BlockDriverInfo {
    pub cluster_size: c_int,
    pub vm_state_offset: i64,
    pub is_dirty: bool,
    pub unallocated_blocks_are_zero: bool,
    pub can_write_zeroes_with_unmap: bool,
    pub needs_compressed_writes: bool,
}

#[repr(C)]
pub struct ImageInfoSpecific {
    pub kind: c_int, /* ImageInfoSpecificKind */
    pub date: *mut c_void, /* type depends on kind */
}

#[repr(C)]
pub struct QEMUIOVector {
    pub iov: *mut iovec,
    pub niov: c_int,
    pub nalloc: c_int,
    pub size: size_t,
}

#[repr(C)]
pub struct iovec {
    pub iov_base: *mut c_void,
    pub iov_len: size_t,
}

#[repr(C)]
pub struct QEMUSnapshotInfo {
    pub id_str: [u8; 128],
    pub name: [u8; 256],
    pub vm_state_size: u64,
    pub date_sec: u32,
    pub date_nsec: u32,
    pub vm_clock_nsec: u64,
}

type BlockDriverAmendStatusCB = extern fn(bs: *mut BlockDriverState,
                                          offset: i64, total_work_size: i64,
                                          opaque: *mut c_void);

/* Opaque types */
pub enum AioContext {}
pub enum BlockReopenQueue {}
pub enum Error {}
pub enum QDict {}
pub enum QemuOpts {}
pub enum QemuOptsList {}

/* Used for deprecated function pointers */
type DeprecatedFn = extern fn();


impl BlockDriver {
    pub fn new(name: &'static str, instance_size: i32) -> BlockDriver
    {
        BlockDriver {
            format_name: name.as_ptr(),
            instance_size: instance_size,

            is_filter: false,

            bdrv_recurse_is_first_non_filter: None,

            bdrv_probe: None,
            bdrv_probe_device: None,

            bdrv_parse_filename: None,
            bdrv_needs_filename: false,

            supports_backing: false,

            bdrv_reopen_prepare: None,
            bdrv_reopen_commit: None,
            bdrv_reopen_abort: None,
            bdrv_join_options: None,

            bdrv_open: None,
            bdrv_file_open: None,
            bdrv_close: None,
            bdrv_create: None,
            bdrv_set_key: None,
            bdrv_make_empty: None,

            bdrv_refresh_filename: None,

            bdrv_aio_readv: None,
            bdrv_aio_writev: None,
            bdrv_aio_flush: None,
            bdrv_aio_pdiscard: None,

            bdrv_co_readv: None,
            bdrv_co_preadv: None,
            bdrv_co_writev: None,
            bdrv_co_writev_flags: None,
            bdrv_co_pwritev: None,

            bdrv_co_pwrite_zeroes: None,
            bdrv_co_pdiscard: None,
            bdrv_co_get_block_status: None,

            bdrv_invalidate_cache: None,
            bdrv_inactivate: None,

            bdrv_co_flush: None,

            bdrv_co_flush_to_disk: None,

            bdrv_co_flush_to_os: None,

            protocol_name: ptr::null(),
            bdrv_truncate: None,

            bdrv_getlength: None,
            has_variable_length: false,
            bdrv_get_allocated_file_size: None,

            bdrv_co_pwritev_compressed: None,

            bdrv_snapshot_create: None,
            bdrv_snapshot_goto: None,
            bdrv_snapshot_delete: None,
            bdrv_snapshot_list: None,
            bdrv_snapshot_load_tmp: None,

            bdrv_get_info: None,
            bdrv_get_specific_info: None,

            bdrv_save_vmstate: None,
            bdrv_load_vmstate: None,

            bdrv_change_backing_file: None,

            bdrv_is_inserted: None,
            bdrv_media_changed: None,
            bdrv_eject: None,
            bdrv_lock_medium: None,

            bdrv_aio_ioctl: None,
            bdrv_co_ioctl: None,

            create_opts: ptr::null_mut(),

            bdrv_check: None,

            bdrv_amend_options: None,

            bdrv_debug_event: None,
            bdrv_debug_breakpoint: None,
            bdrv_debug_remove_breakpoint: None,
            bdrv_debug_resume: None,
            bdrv_debug_is_suspended: None,

            bdrv_refresh_limits: None,

            bdrv_has_zero_init: None,

            bdrv_detach_aio_context: None,

            bdrv_attach_aio_context: None,

            bdrv_io_plug: None,
            bdrv_io_unplug: None,

            bdrv_probe_blocksizes: None,
            bdrv_probe_geometry: None,

            bdrv_drain: None,

            bdrv_add_child: None,
            bdrv_del_child: None,

            bdrv_check_perm: None,
            bdrv_set_perm: None,
            bdrv_abort_perm_update: None,

            bdrv_child_perm: None,

            list: BlockDriverListEntry {
                le_next: ptr::null_mut(),
                le_prev: ptr::null_mut(),
            },
        }
    }
}


pub fn bdrv_register(bdrv: BlockDriver)
{
    /* Box so it doesn't go away */
    let bdrv_box = Box::new(bdrv);

    unsafe {
        functions::bdrv_register(Box::into_raw(bdrv_box));
    }
}


pub fn error_setg(errp: *mut *mut Error, message: String)
{
    unsafe {
        functions::error_setg_internal(errp, b"<FILE>" as *const u8,
                                       -1, b"<FUNC>" as *const u8,
                                       b"%.*s\0" as *const u8,
                                       message.len() as c_int,
                                       message.as_ptr() as *const u8);
    }
}
