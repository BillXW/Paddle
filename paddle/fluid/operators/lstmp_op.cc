/* Copyright (c) 2016 PaddlePaddle Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License. */

#include "paddle/fluid/operators/lstmp_op.h"
#include <string>

namespace paddle {
namespace operators {

class LSTMPOp : public framework::OperatorWithKernel {
 public:
  using framework::OperatorWithKernel::OperatorWithKernel;

  void InferShape(framework::InferShapeContext* ctx) const override {
    PADDLE_ENFORCE(ctx->HasInput("Input"),
                   "Input(Input) of LSTMP operator should not be null.");
    PADDLE_ENFORCE(ctx->HasInput("Weight"),
                   "Input(Weight) of LSTMP operator should not be null.");
    PADDLE_ENFORCE(ctx->HasInput("ProjWeight"),
                   "Input(ProjWeight) of LSTMP operator should not be null.");
    PADDLE_ENFORCE(ctx->HasInput("Bias"),
                   "Input(Bias) of LSTMP operator should not be null.");

    PADDLE_ENFORCE(ctx->HasOutput("Projection"),
                   "Output(Projection) of LSTMP operator should not be null.");
    PADDLE_ENFORCE(ctx->HasOutput("Cell"),
                   "Output(Cell) of LSTMP operator should not be null.");
    PADDLE_ENFORCE(ctx->HasOutput("BatchGate"),
                   "Output(BatchGate) of LSTMP operator should not be null.");
    PADDLE_ENFORCE(ctx->HasOutput("BatchCellPreAct"),
                   "Output(BatchCellPreAct) of LSTMP operator should not be "
                   "null.");
    PADDLE_ENFORCE(ctx->HasOutput("BatchHidden"),
                   "Output(BatchHidden) of LSTMP operator should not be null.");

    auto in_dims = ctx->GetInputDim("Input");
    PADDLE_ENFORCE_EQ(in_dims.size(), 2,
                      "Input(X)'s rank of LSTMP operator must be 2.");

    int frame_size = in_dims[1] / 4;
    auto w_dims = ctx->GetInputDim("Weight");
    auto proj_dims = ctx->GetInputDim("ProjWeight");
    PADDLE_ENFORCE_EQ(w_dims.size(), 2,
                      "The rank of Input(Weight) should be 2.");
    PADDLE_ENFORCE_EQ(w_dims[0], proj_dims[1],
                      "The first dimension of Input(Weight) "
                      "should be %d.",
                      proj_dims[1]);
    PADDLE_ENFORCE_EQ(w_dims[1], 4 * frame_size,
                      "The second dimension of Input(Weight) "
                      "should be 4 * %d.",
                      frame_size);

    PADDLE_ENFORCE_EQ(proj_dims.size(), 2,
                      "The rank of Input(ProjWeight) should be 2.");
    PADDLE_ENFORCE_EQ(proj_dims[0], frame_size,
                      "The first dimension of Input(ProjWeight) "
                      "should be %d.",
                      frame_size);

    if (ctx->HasInput("H0")) {
      PADDLE_ENFORCE(ctx->HasInput("C0"),
                     "Input(C0) of LSTMP operator should not be null after "
                     "Input(H0) provided.");
      auto h_dims = ctx->GetInputDim("H0");
      auto c_dims = ctx->GetInputDim("C0");
      ctx->SetOutputDim("OrderedP0", {h_dims[0], proj_dims[1]});
    }

    auto b_dims = ctx->GetInputDim("Bias");
    PADDLE_ENFORCE_EQ(b_dims.size(), 2, "The rank of Input(Bias) should be 2.");
    PADDLE_ENFORCE_EQ(b_dims[0], 1,
                      "The first dimension of Input(Bias) should be 1.");

    if (ctx->Attrs().Get<bool>("use_peepholes")) {
      PADDLE_ENFORCE_EQ(b_dims[1], 7 * frame_size,
                        "The second dimension of Input(Bias) should be "
                        "7 * %d if enable peepholes connection",
                        frame_size);
    } else {
      PADDLE_ENFORCE_EQ(b_dims[1], 4 * frame_size,
                        "The second dimension of Input(Bias) should be "
                        "4 * %d if disable peepholes connection",
                        frame_size);
    }

    framework::DDim out_dims({in_dims[0], frame_size});
    framework::DDim proj_out_dims({in_dims[0], proj_dims[1]});
    ctx->SetOutputDim("Projection", proj_out_dims);
    ctx->SetOutputDim("Cell", out_dims);
    ctx->SetOutputDim("BatchGate", in_dims);
    ctx->SetOutputDim("BatchCellPreAct", out_dims);
    ctx->SetOutputDim("BatchHidden", out_dims);
    ctx->ShareLoD("Input", "Projection");
    ctx->ShareLoD("Input", "Cell");
  }

 protected:
  framework::OpKernelType GetExpectedKernelType(
      const framework::ExecutionContext& ctx) const override {
    return framework::OpKernelType(
        ctx.Input<framework::LoDTensor>("Input")->type(), ctx.device_context());
  }
};

class LSTMPOpMaker : public framework::OpProtoAndCheckerMaker {
 public:
  void Make() override {
    AddInput("Input",
             "(LoDTensor) the input for sequence data, which supports "
             "variable-time length input sequence. The underlying tensor in "
             "this LoDTensor is a matrix with shape (T X 4D), where T is the "
             "total time steps in this mini-batch, D is the hidden size.");
    AddInput("H0",
             "(Tensor, optional) the initial hidden state is an optional "
             "input. This is a tensor with shape (N x D), where N is the "
             "batch size and D is the hidden size.")
        .AsDispensable();
    AddInput("C0",
             "(Tensor, optional) the initial cell state is an optional "
             "input. This is a tensor with shape (N x D), where N is the "
             "batch size. `C0` should not be null if `H0` provided.")
        .AsDispensable();
    AddInput("Weight",
             "(Tensor) the learnable hidden-hidden weights."
             " - The shape is (P x 4D), where P is the projection layer size "
             "and  D is the hidden size."
             " - Weight = {W_cr, W_ir, W_fr, W_or}");
    AddInput("ProjWeight",
             "(Tensor) the learnable weight of the projection layer."
             " - The shape is (D x P), where P is the recurrent projection "
             "layer size and  D is the hidden size."
             " - ProjWeight = {W_rh}");
    AddInput("Bias",
             "(Tensor) the learnable biases, which contains two parts: "
             "input-hidden biases and peephole connections weights if "
             "setting `use_peepholes` to `True`. "
             "1. `use_peepholes = False` "
             " - The shape is (1 x 4D). "
             " - Bias = {b_c, b_i, b_f, b_o}."
             "2. `use_peepholes = True` "
             " - The shape is (1 x 7D). "
             " - Bias = {b_c, b_i, b_f, b_o, W_ic, W_fc, W_oc}.");
    AddOutput("Projection",
              "(LoDTensor) the projection of the hidden state of LSTMP "
              "operator. The shape is (T x P), and LoD is the same with the "
              "`Input`.");
    AddOutput("Cell",
              "(LoDTensor) the cell state of LSTMP operator. "
              "The shape is (T x D), and lod is the same with the `Input`.");
    AddOutput("BatchGate",
              "(LoDTensor) This LoDTensor contains input gate, forget gate "
              "and output gate after the activations. This LoDTensor has the "
              "same shape as the reorganized input, which is also be called "
              "batch input. The LoD size is 2. The first-level LoD is the "
              "batch offsets and the second contains the indices, which "
              "denotes the position of reorganized sequence in the raw input.")
        .AsIntermediate();
    AddOutput("BatchCellPreAct",
              "(LoDTensor) the pre-activation cell state reorganized in batch. "
              "This LoDTensor is obtained in the forward and used in the "
              "backward.")
        .AsIntermediate();
    AddOutput("BatchHidden",
              "(LoDTensor) the hidden state reorganized in batch. "
              "This LoDTensor is obtained in the forward and used in the "
              "backward.")
        .AsIntermediate();
    AddOutput("OrderedP0",
              "(Tensor) the projection of the initial hidden state "
              "H0. This is a tensor with shape (N x P), where N is the "
              "batch size and P is the hidden size.")
        .AsIntermediate();
    AddAttr<bool>("use_peepholes",
                  "(bool, defalut: True) "
                  "whether to enable diagonal/peephole connections.")
        .SetDefault(true);
    AddAttr<bool>("is_reverse",
                  "(bool, defalut: False) "
                  "whether to compute reversed LSTMP.")
        .SetDefault(false);
    AddAttr<std::string>(
        "gate_activation",
        "(string, default: sigmoid)"
        "The activation for input gate, forget gate and output "
        "gate, `sigmoid` by default.")
        .SetDefault("sigmoid")
        .InEnum({"sigmoid", "tanh", "relu", "identity"});
    AddAttr<std::string>("cell_activation",
                         "(string, default: tanh)"
                         "The activation for cell output, `tanh` by defalut.")
        .SetDefault("tanh")
        .InEnum({"sigmoid", "tanh", "relu", "identity"});
    AddAttr<std::string>("candidate_activation",
                         "(string, default: tanh)"
                         "The activation for candidate hidden state, "
                         "`tanh` by default.")
        .SetDefault("tanh")
        .InEnum({"sigmoid", "tanh", "relu", "identity"});
    AddAttr<std::string>("proj_activation",
                         "(string, default: tanh)"
                         "The activation for projection output, "
                         "`tanh` by defalut.")
        .SetDefault("tanh")
        .InEnum({"sigmoid", "tanh", "relu", "identity"});
    AddComment(R"DOC(
Long-Short Term Memory with recurrent Projection layer (LSTMP) Operator.

LSTMP has a separate projection layer after the LSTM layer, projecting the 
original hidden state to a lower-dimensional one, which is proposed to reduce 
the number of total parameters and furthermore computational complexity for 
the LSTM, espeacially for the case that the size of output units is relative 
large (https://research.google.com/pubs/archive/43905.pdf). 

The formula is as follows:

$$
i_t = \sigma(W_{ix}x_{t} + W_{ir}r_{t-1} + W_{ic}c_{t-1} + b_i) \\

f_t = \sigma(W_{fx}x_{t} + W_{fr}r_{t-1} + W_{fc}c_{t-1} + b_f) \\

\tilde{c_t} = act_g(W_{cx}x_t + W_{cr}r_{t-1} + b_c) \\

o_t = \sigma(W_{ox}x_{t} + W_{or}r_{t-1} + W_{oc}c_t + b_o) \\

c_t = f_t \odot c_{t-1} + i_t \odot \tilde{c_t} \\

h_t = o_t \odot act_h(c_t) \\

r_t = \overline{act_h}(W_{rh}h_t)
$$

where the W terms denote weight matrices (e.g. $W_{xi}$ is the matrix
of weights from the input gate to the input), $W_{ic}, W_{fc}, W_{oc}$
are diagonal weight matrices for peephole connections. In our implementation,
we use vectors to reprenset these diagonal weight matrices. The b terms
denote bias vectors ($b_i$ is the input gate bias vector), $\sigma$
is the activation, such as logistic sigmoid function, and
$i, f, o$ and $c$ are the input gate, forget gate, output gate,
and cell activation vectors, respectively, all of which have the same size as
the cell output activation vector $h$. Here $h$ is usually called the hidden 
state and $r$ denotes its recurrent projection. And $\tilde{c_t}$ is also 
called the candidate hidden state, whose computation is based on the current 
input and previous hidden state.

The $\odot$ is the element-wise product of the vectors. $act_g$ and $act_h$
are the cell input and cell output activation functions and `tanh` is usually
used for them. $\overline{act_h}$ is the activation function for the 
projection output, usually using `identity` or same as $act_h$.

Note that these $W_{xi}x_{t}, W_{xf}x_{t}, W_{xc}x_{t}, W_{xo}x_{t}$
operations on the input $x_{t}$ are NOT included in this operator.
Users can choose to use fully-connected operator before LSTMP operator.

)DOC");
  }
};

class LSTMPGradOp : public framework::OperatorWithKernel {
 public:
  using framework::OperatorWithKernel::OperatorWithKernel;

  void InferShape(framework::InferShapeContext* ctx) const override {
    PADDLE_ENFORCE(ctx->HasInput("Input"),
                   "Input(Input) of LSTMP operator should not be null.");
    PADDLE_ENFORCE(ctx->HasInput("Projection"),
                   "Input(Projection) of LSTMP operator should not be null.");
    PADDLE_ENFORCE(ctx->HasInput("Cell"),
                   "Input(Cell) of LSTMP operator should not be null.");
    PADDLE_ENFORCE(ctx->HasInput("Weight"),
                   "Input(Weight) of LSTMP operator should not be null.");
    PADDLE_ENFORCE(ctx->HasInput("ProjWeight"),
                   "Input(ProjWeight) of LSTMP operator should not be null.");
    PADDLE_ENFORCE(ctx->HasInput("Bias"),
                   "Input(Bias) of LSTMP operator should not be null.");

    PADDLE_ENFORCE(ctx->HasInput("BatchGate"),
                   "Input(BatchGate) of LSTMP operator should not be null.");
    PADDLE_ENFORCE(ctx->HasInput("BatchCellPreAct"),
                   "Input(BatchGate) of LSTMP operator should not be null.");

    auto SetOutGradDim = [&ctx](const std::string& name) {
      auto g_name = framework::GradVarName(name);
      if (ctx->HasOutput(g_name))
        ctx->SetOutputDim(g_name, ctx->GetInputDim(name));
    };

    SetOutGradDim("Input");
    SetOutGradDim("Weight");
    SetOutGradDim("ProjWeight");
    SetOutGradDim("Bias");
    SetOutGradDim("H0");
    SetOutGradDim("C0");
  }

 protected:
  framework::OpKernelType GetExpectedKernelType(
      const framework::ExecutionContext& ctx) const override {
    return framework::OpKernelType(
        ctx.Input<framework::LoDTensor>("Input")->type(), ctx.device_context());
  }
};

}  // namespace operators
}  // namespace paddle

namespace ops = paddle::operators;
REGISTER_OPERATOR(lstmp, ops::LSTMPOp, ops::LSTMPOpMaker,
                  paddle::framework::DefaultGradOpDescMaker<true>);
REGISTER_OPERATOR(lstmp_grad, ops::LSTMPGradOp);
REGISTER_OP_CPU_KERNEL(
    lstmp, ops::LSTMPKernel<paddle::platform::CPUDeviceContext, float>,
    ops::LSTMPKernel<paddle::platform::CPUDeviceContext, double>);
REGISTER_OP_CPU_KERNEL(
    lstmp_grad, ops::LSTMPGradKernel<paddle::platform::CPUDeviceContext, float>,
    ops::LSTMPGradKernel<paddle::platform::CPUDeviceContext, double>);
