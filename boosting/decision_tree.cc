/* decision_tree.cc                                                -*- C++ -*-
   Jeremy Barnes, 22 March 2004
   Copyright (c) 2004 Jeremy Barnes.  All rights reserved.
   $Source$

   Implementation of the decision tree.
*/

#include "decision_tree.h"
#include "classifier_persist_impl.h"
#include <boost/progress.hpp>
#include <boost/timer.hpp>
#include <functional>
#include "jml/utils/vector_utils.h"
#include "config_impl.h"


using namespace std;
using namespace DB;



namespace ML {


/*****************************************************************************/
/* DECISION_TREE                                                             */
/*****************************************************************************/

Decision_Tree::Decision_Tree()
    : encoding(OE_PROB), optimized_(false)
{
}

Decision_Tree::
Decision_Tree(DB::Store_Reader & store,
              const boost::shared_ptr<const Feature_Space> & fs)
    : optimized_(false)
{
    throw Exception("Decision_Tree constructor(reconst): not implemented");
}
    
Decision_Tree::
Decision_Tree(boost::shared_ptr<const Feature_Space> feature_space,
              const Feature & predicted)
    : Classifier_Impl(feature_space, predicted),
      encoding(OE_PROB),
      optimized_(false)
{
}
    
Decision_Tree::
~Decision_Tree()
{
}
    
void
Decision_Tree::
swap(Decision_Tree & other)
{
    Classifier_Impl::swap(other);
    std::swap(tree, other.tree);
    std::swap(encoding, other.encoding);
    std::swap(optimized_, other.optimized_);
}

namespace {

struct StandardGetFeatures {
    StandardGetFeatures(const Feature_Set & features)
        : features(features)
    {
    }

    const Feature_Set & features;

    Split::Weights operator () (const Split & split) const
    {
        return split.apply(features);
    }
};

struct OptimizedGetFeatures {
    OptimizedGetFeatures(const float * features)
        : features(features)
    {
    }

    const float * features;

    JML_ALWAYS_INLINE Split::Weights operator () (const Split & split) const
    {
        return split.apply(features);
    }
                      
};

struct AccumResults {
    explicit AccumResults(double * accum, int nl, double weight)
        : accum(accum), nl(nl), weight(weight)
    {
    }

    double * accum;
    int nl;
    double weight;

    JML_ALWAYS_INLINE
    void operator () (const Label_Dist & dist, float weight1)
    {
        if (JML_LIKELY(nl == 2)) {
            double factor = weight1 * weight;
            accum[0] += dist[0] * factor;
            accum[1] += dist[1] * factor;
            return;
        }

        for (unsigned i = 0;  i < nl;  ++i)
            accum[i] += dist[i] * weight1 * weight;
    }
};

struct DistResults {
    explicit DistResults(double * accum, int nl)
        : accum(accum), nl(nl)
    {
        std::fill(accum, accum + nl, 0.0);
    }

    double * accum;
    int nl;

    JML_ALWAYS_INLINE
    void operator () (const Label_Dist & dist, float weight)
    {
        for (unsigned i = 0;  i < nl;  ++i)
            accum[i] += dist[i] * weight;
    }
    
    operator Label_Dist () const { return Label_Dist(accum, accum + nl); }
};

struct LabelResults {
    explicit LabelResults(int label)
        : label(label), result(0.0)
    {
    }

    JML_ALWAYS_INLINE
    void operator () (const Label_Dist & dist, float weight)
    {
        result += weight * dist[label];
    }

    int label;
    double result;

    operator double () const { return result; }
};

} // file scope

float
Decision_Tree::
predict(int label, const Feature_Set & features) const
{
    StandardGetFeatures get_features(features);
    LabelResults results(label);

    predict_recursive_impl(get_features, results, tree.root);
    return results;
}

Label_Dist
Decision_Tree::
predict(const Feature_Set & features) const
{
    StandardGetFeatures get_features(features);
    int nl = label_count();
    double accum[nl];
    DistResults results(accum, nl);

    predict_recursive_impl(get_features, results, tree.root);
    return results;
}

bool
Decision_Tree::
optimization_supported() const
{
    return true;
}

bool
Decision_Tree::
predict_is_optimized() const
{
    return optimized_;
}

bool
Decision_Tree::
optimize_impl(Optimization_Info & info)
{
    optimize_recursive(info, tree.root);
    optimized_ = true;
    return true;
}

void
Decision_Tree::
optimize_recursive(Optimization_Info & info,
                   const Tree::Ptr & ptr)
{
    if (!ptr) return;
    if (!ptr.node()) return;

    Tree::Node & node = *ptr.node();

    node.split.optimize(info);
    optimize_recursive(info, node.child_true);
    optimize_recursive(info, node.child_false);
    optimize_recursive(info, node.child_missing);
}

Label_Dist
Decision_Tree::
optimized_predict_impl(const float * features,
                       const Optimization_Info & info) const
{
    OptimizedGetFeatures get_features(features);

    int nl = label_count();
    double accum[nl];
    DistResults results(accum, nl);

    predict_recursive_impl(get_features, results, tree.root);
    return results;
}

void
Decision_Tree::
optimized_predict_impl(const float * features,
                       const Optimization_Info & info,
                       double * accum,
                       double weight) const
{
    OptimizedGetFeatures get_features(features);
    AccumResults results(accum, label_count(), weight);

    predict_recursive_impl(get_features, results, tree.root);
}

float
Decision_Tree::
optimized_predict_impl(int label,
                       const float * features,
                       const Optimization_Info & info) const
{
    OptimizedGetFeatures get_features(features);
    LabelResults results(label);

    predict_recursive_impl(get_features, results, tree.root);
    return results;
}

template<class GetFeatures, class Results>
void
Decision_Tree::
predict_recursive_impl(const GetFeatures & get_features,
                       Results & results,
                       const Tree::Ptr & ptr,
                       double weight) const
{
    if (!ptr) return;

    if (!ptr.node()) {
        results(ptr.leaf()->pred, weight);
        return;
    }

    const Tree::Node & node = *ptr.node();
    
    Split::Weights weights = get_features(node.split);
    
    /* Go down all of the edges that we need to for this example. */
    if (weights[true] > 0.0)
        predict_recursive_impl(get_features, results, node.child_true,
                               weights[true]);
    if (weights[false] > 0.0)
        predict_recursive_impl(get_features, results, node.child_false,
                               weights[false]);
    if (weights[MISSING] > 0.0)
        predict_recursive_impl(get_features, results, node.child_missing,
                               weights[MISSING]);
}

string
Decision_Tree::
print_recursive(int level, const Tree::Ptr & ptr,
                float total_weight) const
{
    string spaces(level * 4, ' ');
    if (ptr.node()) {
        Tree::Node & n = *ptr.node();
        string result;
        float cov = n.examples / total_weight;
        float z_adj = n.z / cov;
        result += spaces 
            + format(" %s (z = %.4f, weight = %.2f, cov = %.2f%%)\n",
                     n.split.print(*feature_space()).c_str(),
                     z_adj, n.examples, cov * 100.0);
        result += spaces + "  true: \n";
        result += print_recursive(level + 1, n.child_true, total_weight);
        result += spaces + "  false: \n";
        result += print_recursive(level + 1, n.child_false, total_weight);
        result += spaces + "  missing: \n";
        result += print_recursive(level + 1, n.child_missing, total_weight);
        return result;
    }
    else if (ptr.leaf()) {
        string result = spaces + "leaf: ";
        Tree::Leaf & l = *ptr.leaf();
        const distribution<float> & dist = l.pred;
        for (unsigned i = 0;  i < dist.size();  ++i)
            if (dist[i] != 0.0) result += format(" %d/%.3f", i, dist[i]);
        float cov = l.examples / total_weight;
        result += format(" (weight = %.2f, cov = %.2f%%)\n",
                         l.examples, cov * 100.0);
        
        return result + "\n";
    }
    else return spaces + "NULL";
}

Explanation
Decision_Tree::
explain(const Feature_Set & feature_set,
        int label,
        double weight) const
{
    Explanation result(feature_set, *feature_space(), label); 

    explain_recursive(result, weight, tree.root, 0);

    return result;
}

void
Decision_Tree::
explain_recursive(Explanation & explanation,
                  double weight,
                  const Tree::Ptr & ptr,
                  const Tree::Node * parent) const
{
    StandardGetFeatures get_features(*explanation.fset);
    int nl = label_count();
    int label = explanation.label;

    if (label < 0 || label >= nl)
        throw Exception("Decision_Tree::explain(): no label");

    if (!ptr) return;

    if (!ptr.node()) {
        // It's a leaf; we give the difference to the parent's feature
        if (parent) {
            explanation.feature_weights[parent->split.feature()]
                += weight
                * (ptr.leaf()->pred.at(label) - parent->pred.at(label));
        }
        else {
            // No parent, therefore it's all bias
            explanation.bias += weight * (ptr.leaf()->pred.at(label));
        }

        return;
    }

    const Tree::Node & node = *ptr.node();
    
    Split::Weights weights = get_features(node.split);

    // Accumulate the weight for this split
    if (!parent)
        explanation.bias += weight * node.pred.at(label);
    else
        explanation.feature_weights[parent->split.feature()]
            += weight
            * (node.pred.at(label) - parent->pred.at(label));
    
    /* Go down all of the edges that we need to for this example. */
    if (weights[true] > 0.0)
        explain_recursive(explanation, weight * weights[true],
                          node.child_true, &node);

    if (weights[false] > 0.0)
        explain_recursive(explanation, weight * weights[false],
                          node.child_false, &node);

    if (weights[MISSING] > 0.0)
        explain_recursive(explanation, weight * weights[MISSING],
                          node.child_missing, &node);
}

std::string
Decision_Tree::
print() const
{
    string result = "Decision tree:\n";
    float total_weight = 0.0;
    if (tree.root.node()) total_weight = tree.root.node()->examples;
    result += print_recursive(0, tree.root, total_weight);
    return result;
}

std::string
Decision_Tree::
summary() const
{
    if (!tree.root)
        return "NULL";
    
    float total_weight = 0.0;
    if (tree.root.node()) total_weight = tree.root.node()->examples;
    
    if (tree.root.node()) {
        Tree::Node & n = *tree.root.node();
        float cov = n.examples / total_weight;
        float z_adj = n.z / cov;
        return "Root: " + n.split.print(*feature_space())
            + format(" (z = %.4f)", z_adj);
    }
    else {
        string result = "leaf: ";
        Tree::Leaf & l = *tree.root.leaf();
        const distribution<float> & dist = l.pred;
        for (unsigned i = 0;  i < dist.size();  ++i)
            if (dist[i] != 0.0) result += format(" %d/%.3f", i, dist[i]);
        return result;
    }
}

namespace {

void all_features_recursive(const Tree::Ptr & ptr,
                            vector<Feature> & result)
{
    if (ptr.node()) {
        Tree::Node & n = *ptr.node();
        result.push_back(n.split.feature());

        all_features_recursive(n.child_true,    result);
        all_features_recursive(n.child_false,   result);
        all_features_recursive(n.child_missing, result);
    }
}

} // file scope

std::vector<ML::Feature>
Decision_Tree::
all_features() const
{
    std::vector<ML::Feature> result;
    all_features_recursive(tree.root, result);
    make_vector_set(result);
    return result;
}

Output_Encoding
Decision_Tree::
output_encoding() const
{
    return encoding;
}

void
Decision_Tree::
serialize(DB::Store_Writer & store) const
{
    store << string("DECISION_TREE");
    store << compact_size_t(3);  // version
    store << compact_size_t(label_count());
    feature_space_->serialize(store, predicted_);
    tree.serialize(store, *feature_space());
    store << encoding;
    store << compact_size_t(12345);  // end marker
}

void
Decision_Tree::
reconstitute(DB::Store_Reader & store,
             const boost::shared_ptr<const Feature_Space> & feature_space)
{
    string id;
    store >> id;

    if (id != "DECISION_TREE")
        throw Exception("Decision_Tree::reconstitute: read bad ID '"
                        + id + "'");

    compact_size_t version(store);
    
    switch (version) {
    case 1: {
        compact_size_t label_count(store);
        Classifier_Impl::init(feature_space, MISSING_FEATURE, label_count);
        tree.reconstitute(store, *feature_space);
        break;
    }
    case 2:
    case 3: {
        compact_size_t label_count(store);
        feature_space->reconstitute(store, predicted_);
        Classifier_Impl::init(feature_space, predicted_);
        tree.reconstitute(store, *feature_space);
        if (version >= 3)
            store >> encoding;
        else encoding = OE_PROB;
        break;
    }
    default:
        throw Exception("Decision tree: Attempt to reconstitute tree of "
                        "unknown version " + ostream_format(version.size_));
    }

    compact_size_t marker(store);
    if (marker != 12345)
        throw Exception("Decision_Tree::reconstitute: read bad marker at end");

    optimized_ = false;
}
    
std::string
Decision_Tree::
class_id() const
{
    return "DECISION_TREE";
}

Decision_Tree *
Decision_Tree::
make_copy() const
{
    return new Decision_Tree(*this);
}

/*****************************************************************************/
/* REGISTRATION                                                              */
/*****************************************************************************/

namespace {

Register_Factory<Classifier_Impl, Decision_Tree> REGISTER("DECISION_TREE");

} // file scope

} // namespace ML
