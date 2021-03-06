/**
 * Copyright (C) 2018 MongoDB Inc.
 *
 * This program is free software: you can redistribute it and/or  modify
 * it under the terms of the GNU Affero General Public License, version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * As a special exception, the copyright holders give permission to link the
 * code of portions of this program with the OpenSSL library under certain
 * conditions as described in each individual source file and distribute
 * linked combinations including the program with the OpenSSL library. You
 * must comply with the GNU Affero General Public License in all respects
 * for all of the code used other than as permitted herein. If you modify
 * file(s) with this exception, you may extend this exception to your
 * version of the file(s), but you are not obligated to do so. If you do not
 * wish to do so, delete this exception statement from your version. If you
 * delete this exception statement from all source files in the program,
 * then also delete it in the license file.
 */

#include "mongo/platform/basic.h"

#include "mongo/db/exec/projection_exec_agg.h"

#include "mongo/db/pipeline/document.h"
#include "mongo/db/pipeline/expression_context.h"
#include "mongo/db/pipeline/parsed_aggregation_projection.h"

namespace mongo {

class ProjectionExecAgg::ProjectionExecutor {
public:
    using ParsedAggregationProjection = parsed_aggregation_projection::ParsedAggregationProjection;
    using ProjectionParseMode = ParsedAggregationProjection::ProjectionParseMode;
    using TransformerType = TransformerInterface::TransformerType;

    ProjectionExecutor(BSONObj projSpec,
                       DefaultIdPolicy defaultIdPolicy,
                       ArrayRecursionPolicy arrayRecursionPolicy) {
        // Construct a dummy ExpressionContext for ParsedAggregationProjection. It's OK to set the
        // ExpressionContext's OperationContext and CollatorInterface to 'nullptr' here; since we
        // ban computed fields from the projection, the ExpressionContext will never be used.
        boost::intrusive_ptr<ExpressionContext> expCtx(new ExpressionContext(nullptr, nullptr));

        // Default projection behaviour is to include _id if the projection spec omits it. If the
        // caller has specified that we should *exclude* _id by default, do so here. We translate
        // DefaultIdPolicy to ParsedAggregationProjection::ProjectionDefaultIdPolicy in order to
        // avoid exposing internal aggregation types to the query system.
        ParsedAggregationProjection::ProjectionDefaultIdPolicy idPolicy =
            (defaultIdPolicy == ProjectionExecAgg::DefaultIdPolicy::kIncludeId
                 ? ParsedAggregationProjection::ProjectionDefaultIdPolicy::kIncludeId
                 : ParsedAggregationProjection::ProjectionDefaultIdPolicy::kExcludeId);

        // By default, $project will recurse through nested arrays. If the caller has specified that
        // it should not, we inhibit it from doing so here. We separate this class' internal enum
        // ArrayRecursionPolicy from ParsedAggregationProjection::ProjectionArrayRecursionPolicy
        // in order to avoid exposing aggregation types to the query system.
        ParsedAggregationProjection::ProjectionArrayRecursionPolicy recursionPolicy =
            (arrayRecursionPolicy == ArrayRecursionPolicy::kRecurseNestedArrays
                 ? ParsedAggregationProjection::ProjectionArrayRecursionPolicy::kRecurseNestedArrays
                 : ParsedAggregationProjection::ProjectionArrayRecursionPolicy::
                       kDoNotRecurseNestedArrays);

        _projection = ParsedAggregationProjection::create(
            expCtx, projSpec, idPolicy, recursionPolicy, ProjectionParseMode::kBanComputedFields);
    }

    std::set<std::string> getExhaustivePaths() const {
        DepsTracker depsTracker;
        _projection->addDependencies(&depsTracker);
        return depsTracker.fields;
    }

    ProjectionType getType() const {
        return (_projection->getType() == TransformerType::kInclusionProjection
                    ? ProjectionType::kInclusionProjection
                    : ProjectionType::kExclusionProjection);
    }

    BSONObj applyProjection(BSONObj inputDoc) const {
        return applyTransformation(Document{inputDoc}).toBson();
    }

    stdx::unordered_set<std::string> applyProjectionToFields(
        const stdx::unordered_set<std::string>& fields) const {
        stdx::unordered_set<std::string> out;

        for (const auto& field : fields) {
            MutableDocument doc;
            const FieldPath f = FieldPath(field);
            doc.setNestedField(f, Value(1.0));
            const Document transformedDoc = applyTransformation(doc.freeze());
            if (!(transformedDoc.getNestedField(f).missing()))
                out.insert(field);
        }

        return out;
    }

private:
    Document applyTransformation(Document inputDoc) const {
        return _projection->applyTransformation(inputDoc);
    }

    std::unique_ptr<ParsedAggregationProjection> _projection;
};

// ProjectionExecAgg's constructor and destructor are defined here, at a point where the
// implementation of ProjectionExecutor is known, so that std::unique_ptr can be used with the
// forward-declared ProjectionExecutor class.
ProjectionExecAgg::ProjectionExecAgg(BSONObj projSpec, std::unique_ptr<ProjectionExecutor> exec)
    : _exec(std::move(exec)), _projSpec(std::move(projSpec)){};

ProjectionExecAgg::~ProjectionExecAgg() = default;

std::unique_ptr<ProjectionExecAgg> ProjectionExecAgg::create(BSONObj projSpec,
                                                             DefaultIdPolicy defaultIdPolicy,
                                                             ArrayRecursionPolicy recursionPolicy) {
    return std::unique_ptr<ProjectionExecAgg>(new ProjectionExecAgg(
        projSpec,
        std::make_unique<ProjectionExecutor>(projSpec, defaultIdPolicy, recursionPolicy)));
}

ProjectionExecAgg::ProjectionType ProjectionExecAgg::getType() const {
    return _exec->getType();
}

BSONObj ProjectionExecAgg::applyProjection(BSONObj inputDoc) const {
    return _exec->applyProjection(inputDoc);
}

stdx::unordered_set<std::string> ProjectionExecAgg::applyProjectionToFields(
    const stdx::unordered_set<std::string>& fields) const {
    return _exec->applyProjectionToFields(fields);
}

std::set<std::string> ProjectionExecAgg::getExhaustivePaths() const {
    return _exec->getExhaustivePaths();
}
}  // namespace mongo
