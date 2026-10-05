// SPDX-License-Identifier: GPL-2.0-only
#include "../enrollment_storage.hpp"
#include "../protocol.hpp"
#include <cassert>
#include <cstdlib>
#include <string>
#include <iostream>
using namespace amazon_remote;
namespace {
ssize_t ShortWrite(int fd,const void *data,size_t length) {return ::write(fd,data,length-1);}
ssize_t WrongWrite(int fd,const void *data,size_t length) {
    auto r=*static_cast<const EnrollmentRecord *>(data);++r.connection;return ::write(fd,&r,length);
}
int SyncError(int) {return -1;}
ssize_t WrongContextWrite(int fd,const void *data,size_t length) {
    auto r=*static_cast<const EnrollmentContextRecord *>(data);++r.expected_handle;return ::write(fd,&r,length);
}
}
int main() {
    char directory[]="/tmp/amazon-enrollment-test-XXXXXX";assert(::mkdtemp(directory));
    const auto path=std::string(directory)+"/intent.bin";
    EnrollmentContextRecord context{};context.instance=42;context.expected_handle=4;
    context.snapshot.begin_ms=1000;context.snapshot.end_ms=1001;
    context.snapshot.checksum=araudit::Checksum(context.snapshot);context.checksum=EnrollmentContextChecksum(context);
    assert(ValidEnrollmentContext(context));
    assert(armodule::PersistEnrollmentContext(path.c_str(),context));
    assert(!armodule::PersistEnrollmentContext(path.c_str(),context));
    assert(!::unlink(path.c_str()));
    for(unsigned variant=0;variant<3;++variant) {
        assert(!armodule::PersistEnrollmentContext(path.c_str(),context,
            variant==0?ShortWrite:variant==1?WrongContextWrite: ::write,variant==2?SyncError: ::fsync));
        assert(!armodule::PersistEnrollmentContext(path.c_str(),context));assert(!::unlink(path.c_str()));
    }
    for(std::size_t offset=0;offset<sizeof context;++offset) {
        auto corrupt=context;reinterpret_cast<unsigned char *>(&corrupt)[offset]^=1;
        assert(!ValidEnrollmentContext(corrupt));
    }
    std::cout<<"PASS context persistence: exclusive checked168-byte snapshot, no-overwrite, short/corrupt/unsynced write preservation and every-byte checksum corruption. NOT hardware proof.\n";
    EnrollmentRecord r{};r.state=unsigned(EnrollmentState::Prepared);r.connection=4;r.instance=42;
    r.requested_ms=1000;r.before_result=0;r.checksum=EnrollmentChecksum(r);
    assert(ValidEnrollment(r));
    assert(armodule::PersistEnrollmentIntent(path.c_str(),r));
    EnrollmentRecord saved{};assert(armodule::ReadExact(path.c_str(),saved) && !std::memcmp(&saved,&r,sizeof r));
    assert(!armodule::PersistEnrollmentIntent(path.c_str(),r)); // immutable intent across restarts
    assert(armodule::ReadExact(path.c_str(),saved) && !std::memcmp(&saved,&r,sizeof r));
    assert(!::unlink(path.c_str()));
    for(unsigned variant=0;variant<3;++variant) {
        assert(!armodule::PersistEnrollmentIntent(path.c_str(),r,
            variant==0 ? ShortWrite : variant==1 ? WrongWrite : ::write,
            variant==2 ? SyncError : ::fsync));
        struct stat st{};assert(!::stat(path.c_str(),&st) && st.st_size>0);
        assert(!armodule::PersistEnrollmentIntent(path.c_str(),r)); // preserve partial/corrupt/unsynced artifact
        assert(!::unlink(path.c_str()));
    }
    for(unsigned variant=0;variant<9;++variant) {
        auto bad=r;
        switch(variant) {
            case 0:bad.instance=0;break;case 1:bad.state=unsigned(EnrollmentState::Accepted);break;
            case 2:bad.request_issued=1;break;case 3:bad.connection=0xffffffff;break;
            case 4:bad.before_result=1;break;case 5:bad.before_matches=1;break;
            case 6:bad.before_count=10;break;case 7:bad.reserved=1;break;case 8:bad.requested_ms=0;break;
        }
        bad.checksum=EnrollmentChecksum(bad);assert(!armodule::PersistEnrollmentIntent(path.c_str(),bad));
        struct stat st{};assert(::stat(path.c_str(),&st) && errno==ENOENT);
    }
    auto corrupt=r;++corrupt.checksum;assert(!armodule::PersistEnrollmentIntent(path.c_str(),corrupt));
    assert(!armodule::PersistEnrollmentIntent((std::string(directory)+"/missing/intent.bin").c_str(),r));
    armodule::Command c{};c.kind=5;c.instance=42;c.issued_ms=1000;c.sequence=2;
#ifdef ARMODULE_NAV15
    assert(armodule::ValidCommand(c,42,1,1001));
    assert(!armodule::ValidCommand(c,42,2,1001));
    assert(!armodule::ValidCommand(c,43,1,1001));
    assert(!armodule::ValidCommand(c,42,1,3001));
    assert(!armodule::ValidCommand(c,42,1,999));
    c.reserved=1;assert(!armodule::ValidCommand(c,42,1,1001));
    c.reserved=0;c.kind=6;assert(!armodule::ValidCommand(c,42,1,1001));
#else
    assert(!armodule::ValidCommand(c,42,1,1001)); // older variants cannot enroll
#endif
    assert(!::rmdir(directory));
    std::cout<<"PASS enrollment: exclusive checked intent, no-overwrite, partial/corrupt/sync failures, validation, replay/staleness and variant-scoped command. NOT hardware proof.\n";
}
