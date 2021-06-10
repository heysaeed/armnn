//
// Copyright © 2021 Arm Ltd and Contributors. All rights reserved.
// SPDX-License-Identifier: MIT
//

#pragma once

#include <backendsCommon/Workload.hpp>

#include <arm_compute/core/Error.h>
#include <arm_compute/runtime/CL/functions/CLElementWiseUnaryLayer.h>

namespace armnn
{

arm_compute::Status ClSinWorkloadValidate(const TensorInfo& input, const TensorInfo& output);

class ClSinWorkload : public BaseWorkload<ElementwiseUnaryQueueDescriptor>
{
public:
    ClSinWorkload(const ElementwiseUnaryQueueDescriptor& descriptor,
                  const WorkloadInfo& info,
                  const arm_compute::CLCompileContext& clCompileContext);
    virtual void Execute() const override;

private:
    mutable arm_compute::CLSinLayer m_SinLayer;
};

} // namespace armnn