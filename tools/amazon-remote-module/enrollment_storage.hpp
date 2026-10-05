// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "io.hpp"
#include "../../mc_mitm/source/amazon_remote/enrollment.hpp"
#include <fcntl.h>

namespace armodule {
// Exclusive creation: an existing or partial intent prevents another request,
// including across module restart. Never delete/truncate it from the module.
// Injectable syscalls exercise actual publication failures in host tests.
inline bool PersistEnrollmentIntent(const char *path, const amazon_remote::EnrollmentRecord &r,
        ssize_t (*write_record)(int,const void *,size_t)=::write,
        int (*sync_record)(int)=::fsync) {
    using namespace amazon_remote;
    if(!ValidEnrollment(r) || r.state!=unsigned(EnrollmentState::Prepared) ||
       r.request_issued || !r.connection || r.connection==0xffffffff || !r.requested_ms ||
       r.before_result || r.before_matches || r.before_count>=10) return false;
    const int fd=::open(path,O_WRONLY|O_CREAT|O_EXCL,0600);
    if(fd<0)return false;
    const bool wrote=write_record(fd,&r,sizeof r)==ssize_t(sizeof r);
    const bool synced=sync_record(fd)==0;
    const bool closed=::close(fd)==0;
    EnrollmentRecord saved{};
    return wrote && synced && closed && ReadExact(path,saved) &&
        ValidEnrollment(saved) && !std::memcmp(&saved,&r,sizeof r);
}
}
