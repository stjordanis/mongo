/**
 *    Copyright (C) 2018-present MongoDB, Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the Server Side Public License, version 1,
 *    as published by MongoDB, Inc.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    Server Side Public License for more details.
 *
 *    You should have received a copy of the Server Side Public License
 *    along with this program. If not, see
 *    <http://www.mongodb.com/licensing/server-side-public-license>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the Server Side Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#pragma once

#include "mongo/base/status.h"
#include "mongo/stdx/functional.h"
#include "mongo/util/future.h"

namespace mongo {

/**
 * Provides the minimal api for a simple out of line executor that can run non-cancellable
 * callbacks.
 *
 * Adds in a minimal amount of support for futures.
 *
 * The contract for scheduling work on an executor is that it never blocks the caller.  It doesn't
 * necessarily need to offer forward progress guarantees, but actual calls to schedule() should not
 * deadlock.
 */
class OutOfLineExecutor {
public:
    using Task = unique_function<void(Status)>;

public:
    /**
     * Invokes the callback on the executor, as in schedule(), returning a future with its result.
     * That future may be ready by the time the caller returns, which means that continuations
     * chained on the returned future may be invoked on the caller of execute's stack.
     */
    template <typename Callback>
    Future<FutureContinuationResult<Callback>> execute(Callback&& cb) {
        auto[promise, future] = makePromiseFuture<FutureContinuationResult<Callback>>();

        schedule([ cb = std::forward<Callback>(cb), p = std::move(promise) ](auto status) mutable {
            if (!status.isOK()) {
                p.setError(status);
                return;
            }
            p.setWith(std::move(cb));
        });

        return std::move(future);
    }

    /**
     * Delegates invocation of the Task to this executor
     *
     * Execution of the Task can happen in one of three contexts:
     * * By default, on an execution context maintained by the OutOfLineExecutor (i.e. a thread).
     * * During shutdown, on the execution context of shutdown/join/dtor for the OutOfLineExecutor.
     * * Post-shutdown, on the execution context of the calling code.
     *
     * The Task will be passed a Status schedStatus that is either:
     * * schedStatus.isOK() if the function is run in an out-of-line context
     * * isCancelationError(schedStatus.code()) if the function is run in an inline context
     *
     * All of this is to say: CHECK YOUR STATUS.
     */
    virtual void schedule(Task func) = 0;

protected:
    ~OutOfLineExecutor() noexcept {}
};

}  // namespace mongo
