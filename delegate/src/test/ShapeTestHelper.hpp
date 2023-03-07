//
// Copyright © 2021, 2023 Arm Ltd and Contributors. All rights reserved.
// SPDX-License-Identifier: MIT
//

#pragma once

#include "TestUtils.hpp"

#include <armnn_delegate.hpp>

#include <flatbuffers/flatbuffers.h>
#include <tensorflow/lite/interpreter.h>
#include <tensorflow/lite/kernels/register.h>
#include <tensorflow/lite/model.h>
#include <tensorflow/lite/schema/schema_generated.h>
#include <tensorflow/lite/version.h>

#include <doctest/doctest.h>

namespace
{
std::vector<char> CreateShapeTfLiteModel(tflite::TensorType inputTensorType,
                                         tflite::TensorType outputTensorType,
                                         const std::vector<int32_t>& inputTensorShape,
                                         const std::vector<int32_t>& outputTensorShape,
                                         float quantScale = 1.0f,
                                         int quantOffset = 0)
{
    using namespace tflite;
    flatbuffers::FlatBufferBuilder flatBufferBuilder;

    std::vector<flatbuffers::Offset<tflite::Buffer>> buffers;
    buffers.push_back(CreateBuffer(flatBufferBuilder));
    buffers.push_back(CreateBuffer(flatBufferBuilder));
    buffers.push_back(CreateBuffer(flatBufferBuilder));

    auto quantizationParameters =
             CreateQuantizationParameters(flatBufferBuilder,
                                          0,
                                          0,
                                          flatBufferBuilder.CreateVector<float>({ quantScale }),
                                          flatBufferBuilder.CreateVector<int64_t>({ quantOffset }));

    std::array<flatbuffers::Offset<Tensor>, 2> tensors;
    tensors[0] = CreateTensor(flatBufferBuilder,
                              flatBufferBuilder.CreateVector<int32_t>(inputTensorShape.data(),
                                                                      inputTensorShape.size()),
                              inputTensorType,
                              1,
                              flatBufferBuilder.CreateString("input"),
                              quantizationParameters);
    tensors[1] = CreateTensor(flatBufferBuilder,
                              flatBufferBuilder.CreateVector<int32_t>(outputTensorShape.data(),
                                                                      outputTensorShape.size()),
                              outputTensorType,
                              2,
                              flatBufferBuilder.CreateString("output"),
                              quantizationParameters);

    const std::vector<int32_t> operatorInputs({ 0 });
    const std::vector<int32_t> operatorOutputs({ 1 });

    flatbuffers::Offset<Operator> shapeOperator =
                                      CreateOperator(flatBufferBuilder,
                                                     0,
                                                     flatBufferBuilder.CreateVector<int32_t>(operatorInputs.data(),
                                                                                             operatorInputs.size()),
                                                     flatBufferBuilder.CreateVector<int32_t>(operatorOutputs.data(),
                                                                                             operatorOutputs.size()),
                                                     BuiltinOptions_ShapeOptions,
                                                     CreateShapeOptions(flatBufferBuilder, outputTensorType).Union());

    flatbuffers::Offset<flatbuffers::String> modelDescription =
        flatBufferBuilder.CreateString("ArmnnDelegate: SHAPE Operator Model");

    flatbuffers::Offset<OperatorCode> operatorCode =
        CreateOperatorCode(flatBufferBuilder, tflite::BuiltinOperator_SHAPE);

    const std::vector<int32_t>    subgraphInputs({ 0 });
    const std::vector<int32_t>    subgraphOutputs({ 1 });

    flatbuffers::Offset<SubGraph> subgraph =
        CreateSubGraph(flatBufferBuilder,
                       flatBufferBuilder.CreateVector(tensors.data(), tensors.size()),
                       flatBufferBuilder.CreateVector<int32_t>(subgraphInputs.data(),
                                                               subgraphInputs.size()),
                       flatBufferBuilder.CreateVector<int32_t>(subgraphOutputs.data(),
                                                               subgraphOutputs.size()),
                       flatBufferBuilder.CreateVector(&shapeOperator, 1));

    flatbuffers::Offset<Model> flatbufferModel =
        CreateModel(flatBufferBuilder,
                    TFLITE_SCHEMA_VERSION,
                    flatBufferBuilder.CreateVector(&operatorCode, 1),
                    flatBufferBuilder.CreateVector(&subgraph, 1),
                    modelDescription,
                    flatBufferBuilder.CreateVector(buffers.data(), buffers.size()));

    flatBufferBuilder.Finish(flatbufferModel);

    return std::vector<char>(flatBufferBuilder.GetBufferPointer(),
                             flatBufferBuilder.GetBufferPointer() + flatBufferBuilder.GetSize());
}

template<typename T, typename K>
void ShapeTest(tflite::TensorType inputTensorType,
               tflite::TensorType outputTensorType,
               std::vector<armnn::BackendId>& backends,
               std::vector<int32_t>& inputShape,
               std::vector<T>& inputValues,
               std::vector<K>& expectedOutputValues,
               std::vector<int32_t>& expectedOutputShape,
               float quantScale = 1.0f,
               int quantOffset = 0)
{
    using namespace tflite;
    std::vector<char> modelBuffer = CreateShapeTfLiteModel(inputTensorType,
                                                           outputTensorType,
                                                           inputShape,
                                                           expectedOutputShape,
                                                           quantScale,
                                                           quantOffset);

    const Model* tfLiteModel = GetModel(modelBuffer.data());

    // Create TfLite Interpreters
    std::unique_ptr<Interpreter> armnnDelegate;

    CHECK(InterpreterBuilder(tfLiteModel, ::tflite::ops::builtin::BuiltinOpResolver())
              (&armnnDelegate) == kTfLiteOk);
    CHECK(armnnDelegate != nullptr);
    CHECK(armnnDelegate->AllocateTensors() == kTfLiteOk);

    std::unique_ptr<Interpreter> tfLiteDelegate;

    CHECK(InterpreterBuilder(tfLiteModel, ::tflite::ops::builtin::BuiltinOpResolver())
              (&tfLiteDelegate) == kTfLiteOk);
    CHECK(tfLiteDelegate != nullptr);
    CHECK(tfLiteDelegate->AllocateTensors() == kTfLiteOk);

    // Create the ArmNN Delegate
    armnnDelegate::DelegateOptions delegateOptions(backends);

    std::unique_ptr < TfLiteDelegate, decltype(&armnnDelegate::TfLiteArmnnDelegateDelete) >
        theArmnnDelegate(armnnDelegate::TfLiteArmnnDelegateCreate(delegateOptions),
                         armnnDelegate::TfLiteArmnnDelegateDelete);

    CHECK(theArmnnDelegate != nullptr);

    // Modify armnnDelegateInterpreter to use armnnDelegate
    CHECK(armnnDelegate->ModifyGraphWithDelegate(theArmnnDelegate.get()) == kTfLiteOk);

    // Set input data
    armnnDelegate::FillInput<T>(tfLiteDelegate, 0, inputValues);
    armnnDelegate::FillInput<T>(armnnDelegate, 0, inputValues);

    // Run EnqueWorkload
    CHECK(tfLiteDelegate->Invoke() == kTfLiteOk);
    CHECK(armnnDelegate->Invoke() == kTfLiteOk);

    // Compare output data
    armnnDelegate::CompareOutputData<K>(tfLiteDelegate,
                                        armnnDelegate,
                                        expectedOutputShape,
                                        expectedOutputValues,
                                        0);

    tfLiteDelegate.reset(nullptr);
    armnnDelegate.reset(nullptr);
}

} // anonymous namespace
