/* layer.cc
   Jeremy Barnes, 20 October 2009
   Copyright (c) 2009 Jeremy Barnes.  All rights reserved.

*/

#include "layer.h"
#include "db/persistent.h"
#include "arch/demangle.h"
#include "algebra/matrix_ops.h"
#include "arch/simd_vector.h"


using namespace std;
using namespace ML::DB;

namespace ML {


/*****************************************************************************/
/* LAYER                                                                     */
/*****************************************************************************/

/** A basic layer of a neural network.  Other kinds of layers can be built on
    this base.
*/

Layer::
Layer()
{
}

distribution<float>
Layer::
apply(const distribution<float> & input) const
{
    if (input.size() != inputs())
        throw Exception("Layer::apply(): invalid number of inputs");
    distribution<float> result(outputs());
    apply(&input[0], &result[0]);
    return result;
}

distribution<double>
Layer::
apply(const distribution<double> & input) const
{
    if (input.size() != inputs())
        throw Exception("Layer::apply(): invalid number of inputs");
    distribution<double> result(outputs());
    apply(&input[0], &result[0]);
    return result;
}
        
void
Layer::
apply(const distribution<float> & input,
      distribution<float> & output) const
{
    if (input.size() != inputs())
        throw Exception("Layer::apply(): invalid number of inputs");
    output.resize(outputs());
    apply(&input[0], &output[0]);
}

void
Layer::
apply(const distribution<double> & input,
      distribution<double> & output) const
{
    if (input.size() != inputs())
        throw Exception("Layer::apply(): invalid number of inputs");
    output.resize(outputs());
    apply(&input[0], &output[0]);
}

void
Layer::
apply(const float * input,
      float * output) const
{
    int ni = inputs(), no = outputs();
    float pre[ni];
    preprocess(input, pre);
    float act[no];
    activation(pre, act);
    transfer(act, output);
}

void
Layer::
apply(const double * input,
      double * output) const
{
    int ni = inputs(), no = outputs();
    double pre[ni];
    preprocess(input, pre);
    double act[no];
    activation(pre, act);
    transfer(act, output);
}

void
Layer::
preprocess(const float * input,
           float * preprocessed) const
{
    if (input == preprocessed) return;
    int ni = inputs();
    std::copy(input, input + ni, preprocessed);
}

void
Layer::
preprocess(const double * input,
           double * preprocessed) const
{
    if (input == preprocessed) return;
    int ni = inputs();
    std::copy(input, input + ni, preprocessed);
}

distribution<float>
Layer::
preprocess(const distribution<float> & input) const
{
    int ni = inputs();
    distribution<float> pre(ni);
    activation(&input[0], &pre[0]);
    return pre;
}

distribution<double>
Layer::
preprocess(const distribution<double> & input) const
{
    int ni = inputs();
    distribution<double> pre(ni);
    activation(&input[0], &pre[0]);
    return pre;
}

void
Layer::
activation(const float * preprocessed,
           float * act) const
{
    int ni = inputs(), no = outputs();
    double preprocessedd[ni], actd[no];
    std::copy(preprocessed, preprocessed + ni, preprocessedd);
    activation(preprocessedd, actd);
    std::copy(actd, actd + no, act);
}

distribution<float>
Layer::
activation(const distribution<float> & preprocessed) const
{
    int no = outputs();
    distribution<float> output(no);
    activation(&preprocessed[0], &output[0]);
    return output;
}

distribution<double>
Layer::
activation(const distribution<double> & preprocessed) const
{
    int no = outputs();
    distribution<double> output(no);
    activation(&preprocessed[0], &output[0]);
    return output;
}

template<typename Float>
void
Layer::
transfer(const Float * activation, Float * outputs, int nvals,
         Transfer_Function_Type transfer_function)
{
    switch (transfer_function) {
    case TF_IDENTITY:
        std::copy(activation, activation + nvals, outputs);
        return;
        
    case TF_LOGSIG:
        for (unsigned i = 0;  i < nvals;  ++i) {
            // See https://bugzilla.redhat.com/show_bug.cgi?id=521190
            // for why we use double version of exp
            outputs[i] = 1.0 / (1.0 + exp((double)-activation[i]));
        }
        break;
        
    case TF_TANH:
        for (unsigned i = 0;  i < nvals;  ++i)
            outputs[i] = tanh(activation[i]);
        break;
        
    case TF_LOGSOFTMAX: {
        double total = 0.0;
        
        for (unsigned i = 0;  i < nvals;  ++i) {
            // See https://bugzilla.redhat.com/show_bug.cgi?id=521190
            // for why we use double version of exp
            total += (outputs[i] = exp((double)activation[i]));
        }

        double factor = 1.0 / total;

        for (unsigned i = 0;  i < nvals;  ++i)
            outputs[i] *= factor;

        break;
    }

    default:
        throw Exception("Layer::transfer(): invalid transfer_function");
    }
}

void
Layer::
transfer(const float * activation, float * outputs) const
{
    transfer(activation, outputs, this->outputs(), transfer_function);
}

void
Layer::
transfer(const double * activation, double * outputs) const
{
    transfer(activation, outputs, this->outputs(), transfer_function);
}

distribution<float>
Layer::
transfer(const distribution<float> & activation) const
{
    int no = outputs();
    distribution<float> output(no);
    transfer(&activation[0], &output[0]);
    return output;
}

distribution<double>
Layer::
transfer(const distribution<double> & activation) const
{
    int no = outputs();
    distribution<double> output(no);
    transfer(&activation[0], &output[0]);
    return output;
}

template<class Float>
void
Layer::
derivative(const Float * outputs, Float * deriv, int nvals,
           Transfer_Function_Type transfer_function)
{
    switch (transfer_function) {

    case TF_IDENTITY:
        std::fill(deriv, deriv + nvals, 1.0);
        break;
        
    case TF_LOGSIG:
        for (unsigned i = 0;  i < nvals;  ++i)
            deriv[i] = outputs[i] * (1.0 - outputs[i]);
        break;
        
    case TF_TANH:
        for (unsigned i = 0;  i < nvals;  ++i)
            deriv[i] = 1.0 - (outputs[i] * outputs[i]);
        break;

    case TF_LOGSOFTMAX:
        for (unsigned i = 0;  i < nvals;  ++i)
            deriv[i] = 1.0 / outputs[i];
        break;
        
    default:
        throw Exception("Layer::transfer(): invalid transfer_function");
    }
}

distribution<float>
Layer::
derivative(const distribution<float> & outputs) const
{
    if (outputs.size() != this->outputs())
        throw Exception("derivative(): wrong size");
    int no = this->outputs();
    distribution<float> result(no);
    derivative(&outputs[0], &result[0], no, transfer_function);
    return result;
}

distribution<double>
Layer::
derivative(const distribution<double> & outputs) const
{
    if (outputs.size() != this->outputs())
        throw Exception("derivative(): wrong size");
    int no = this->outputs();
    distribution<double> result(no);
    derivative(&outputs[0], &result[0], no, transfer_function);
    return result;
}

void
Layer::
derivative(const float * outputs,
           float * derivatives) const
{
    int no = this->outputs();
    derivative(outputs, derivatives, no, transfer_function);
}

void
Layer::
derivative(const double * outputs,
           double * derivatives) const
{
    int no = this->outputs();
    derivative(outputs, derivatives, no, transfer_function);
}

void
Layer::
deltas(const float * outputs, const float * errors, float * deltas) const
{
    derivative(outputs, deltas);
    int no = this->outputs();
    for (unsigned i = 0;  i < no;  ++i)
        deltas[i] *= errors[i];
}

void
Layer::
deltas(const double * outputs, const double * errors, double * deltas) const
{
    derivative(outputs, deltas);
    int no = this->outputs();
    for (unsigned i = 0;  i < no;  ++i)
        deltas[i] *= errors[i];
}

void
Layer::
validate() const
{
    // Default does none
}

std::pair<float, float>
Layer::
targets(float maximum, Transfer_Function_Type transfer_function)
{
    switch (transfer_function) {
    case TF_TANH:
    case TF_IDENTITY: return std::make_pair(-maximum, maximum);
    case TF_LOGSOFTMAX:
    case TF_LOGSIG: return std::make_pair(0.0f, maximum);
    default:
        throw Exception("Layer::targets(): invalid transfer_function");
    }
}



/*****************************************************************************/
/* DENSE_LAYER                                                              */
/*****************************************************************************/

/** A simple one way layer with dense connections. */

template<typename Float>
Dense_Layer<Float>::
Dense_Layer()
{
}

template<typename Float>
Dense_Layer<Float>::
Dense_Layer(size_t inputs, size_t units,
            Transfer_Function_Type transfer_function)
    : weights(boost::extents[inputs][units]), bias(units),
      missing_replacements(inputs)
{
    this->transfer_function = transfer_function;
    zero_fill();
}

template<typename Float>
Dense_Layer<Float>::
Dense_Layer(size_t inputs, size_t units,
            Transfer_Function_Type transfer_function,
            Thread_Context & thread_context,
            float limit)
    : weights(boost::extents[inputs][units]), bias(units),
      missing_replacements(inputs)
{
    this->transfer_function = transfer_function;
    if (limit == -1.0)
        limit = 1.0 / sqrt(inputs);
    random_fill(limit, thread_context);
}

template<typename Float>
std::string
Dense_Layer<Float>::
print() const
{
    size_t ni = inputs(), no = outputs();
    string result = format("{ layer: %zd inputs, %zd neurons, function %d\n",
                           inputs(), outputs(), transfer_function);
    result += "  weights: \n";
    for (unsigned i = 0;  i < ni;  ++i) {
        result += "    [ ";
        for (unsigned j = 0;  j < no;  ++j)
            result += format("%8.4f", weights[i][j]);
        result += " ]\n";
    }
    result += "  bias: \n    [ ";
    for (unsigned j = 0;  j < no;  ++j)
        result += format("%8.4f", bias[j]);
    result += " ]\n";
    
    result += "  missing replacements: \n    [ ";
    for (unsigned j = 0;  j < no;  ++j)
        result += format("%8.4f", bias[j]);
    result += " ]\n";
    
    result += "}\n";
    
    return result;
}

template<typename Float>
std::string
Dense_Layer<Float>::
type() const
{
    return "Dense_Layer<" + demangle(typeid(Float).name()) + ">";
}

template<typename Float>
void
Dense_Layer<Float>::
serialize(DB::Store_Writer & store) const
{
    store << compact_size_t(1) << string("PERCEPTRON LAYER");
    store << compact_size_t(inputs()) << compact_size_t(outputs());
    store << weights << bias << missing_replacements;
    store << transfer_function;
}

template<typename Float>
void
Dense_Layer<Float>::
reconstitute(DB::Store_Reader & store)
{
    compact_size_t version(store);
    if (version != 1)
        throw Exception("invalid layer version");

    string s;
    store >> s;
    if (s != "PERCEPTRON LAYER")
        throw Exception("invalid layer start " + s);
    store >> weights >> bias >> missing_replacements;
    store >> transfer_function;
}

template<typename Float>
void
Dense_Layer<Float>::
preprocess(const float * input,
           float * activation) const
{
    int ni = inputs();

    for (unsigned i = 0;  i < ni;  ++i)
        activation[i] = isnan(input[i]) ? missing_replacements[i] : input[i];
}

template<typename Float>
void
Dense_Layer<Float>::
preprocess(const double * input,
           double * activation) const
{
    int ni = inputs();

    for (unsigned i = 0;  i < ni;  ++i)
        activation[i] = isnan(input[i]) ? missing_replacements[i] : input[i];
}

template<typename Float>
void
Dense_Layer<Float>::
activation(const float * pre,
           float * activation) const
{
    int ni = inputs(), no = outputs();
    double accum[no];  // Accumulate in double precision to improve rounding
    std::copy(bias.begin(), bias.end(), accum);

    for (unsigned i = 0;  i < ni;  ++i)
        SIMD::vec_add(accum, pre[i], &weights[i][0], accum, no);
    
    std::copy(accum, accum + no, activation);
}

template<typename Float>
void
Dense_Layer<Float>::
activation(const double * pre,
           double * activation) const
{
    int ni = inputs(), no = outputs();
    double accum[no];
    std::copy(bias.begin(), bias.end(), accum);
    for (unsigned i = 0;  i < ni;  ++i)
        SIMD::vec_add(accum, pre[i], &weights[i][0], accum, no);
    
    std::copy(accum, accum + no, activation);
}

template<typename Float>
void
Dense_Layer<Float>::
random_fill(float limit, Thread_Context & context)
{
    int ni = weights.shape()[0], no = weights.shape()[1];
    
    for (unsigned j = 0;  j < no;  ++j)
        for (unsigned i = 0;  i < ni;  ++i)
            weights[i][j] = limit * (context.random01() * 2.0f - 1.0f);
    
    if (no != bias.size())
        throw Exception("bias sized wrong");

    for (unsigned o = 0;  o < bias.size();  ++o)
        bias[o] = limit * (context.random01() * 2.0f - 1.0f);

    for (unsigned i = 0;  i < ni;  ++i)
        missing_replacements[i] = limit * (context.random01() * 2.0f - 1.0f);
}

template<typename Float>
void
Dense_Layer<Float>::
zero_fill()
{
    int ni = weights.shape()[0], no = weights.shape()[1];
    
    for (unsigned j = 0;  j < no;  ++j)
        for (unsigned i = 0;  i < ni;  ++i)
            weights[i][j] = 0.0;
    
    if (no != bias.size())
        throw Exception("bias sized wrong");

    bias.fill(0.0);
    missing_replacements.fill(0.0);
}

template<typename Float>
void
Dense_Layer<Float>::
validate() const
{
    if (weights.shape()[1] != bias.size())
        throw Exception("perceptron laye has bad shape");

    int ni = weights.shape()[0], no = weights.shape()[1];
    
    bool has_nonzero = false;

    for (unsigned j = 0;  j < no;  ++j) {
        for (unsigned i = 0;  i < ni;  ++i) {
            if (!finite(weights[i][j]))
                throw Exception("perceptron layer has non-finite weights");
            if (weights[i][j] != 0.0)
                has_nonzero = true;
        }
    }
    
    if (!has_nonzero)
        throw Exception("perceptron layer has all zero weights");

    if (no != bias.size())
        throw Exception("bias sized wrong");

    for (unsigned o = 0;  o < bias.size();  ++o)
        if (!finite(bias[o]))
            throw Exception("perceptron layer has non-finite bias");

    if (ni != missing_replacements.size())
        throw Exception("missing replacements sized wrong");

    for (unsigned i = 0;  i < missing_replacements.size();  ++i)
        if (!finite(missing_replacements[i]))
            throw Exception("perceptron layer has non-finite missing replacement");
}

template<typename Float>
size_t
Dense_Layer<Float>::
parameter_count() const
{
    return (inputs() * 1) * (outputs() + 1);
}

template class Dense_Layer<float>;
template class Dense_Layer<double>;


} // namespace ML
