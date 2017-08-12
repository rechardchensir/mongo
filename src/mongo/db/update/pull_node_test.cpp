/**
 * Copyright (C) 2017 MongoDB Inc.
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

#include "mongo/db/update/pull_node.h"

#include "mongo/bson/mutable/algorithm.h"
#include "mongo/bson/mutable/mutable_bson_test_utils.h"
#include "mongo/db/json.h"
#include "mongo/db/query/collation/collator_interface_mock.h"
#include "mongo/db/update/update_node_test_fixture.h"
#include "mongo/unittest/death_test.h"
#include "mongo/unittest/unittest.h"

namespace mongo {
namespace {

using PullNodeTest = UpdateNodeTest;
using mongo::mutablebson::Element;
using mongo::mutablebson::countChildren;

TEST(PullNodeTest, InitWithBadMatchExpressionFails) {
    auto update = fromjson("{$pull: {a: {b: {$foo: 1}}}}");
    const CollatorInterface* collator = nullptr;
    PullNode node;
    auto status = node.init(update["$pull"]["a"], collator);
    ASSERT_NOT_OK(status);
    ASSERT_EQUALS(ErrorCodes::BadValue, status);
}

TEST(PullNodeTest, InitWithBadTopLevelOperatorFails) {
    auto update = fromjson("{$pull: {a: {$foo: 1}}}");
    const CollatorInterface* collator = nullptr;
    PullNode node;
    auto status = node.init(update["$pull"]["a"], collator);
    ASSERT_NOT_OK(status);
    ASSERT_EQUALS(ErrorCodes::BadValue, status);
}

TEST_F(PullNodeTest, TargetNotFound) {
    auto update = fromjson("{$pull : {a: {$lt: 1}}}");
    const CollatorInterface* collator = nullptr;
    PullNode node;
    ASSERT_OK(node.init(update["$pull"]["a"], collator));

    mutablebson::Document doc(fromjson("{}"));
    setPathToCreate("a");
    addIndexedPath("a");
    auto result = node.apply(getApplyParams(doc.root()));
    ASSERT_TRUE(result.noop);
    ASSERT_FALSE(result.indexesAffected);
    ASSERT_EQUALS(fromjson("{}"), doc);
    ASSERT_TRUE(doc.isInPlaceModeEnabled());
    ASSERT_EQUALS(fromjson("{}"), getLogDoc());
}

TEST_F(PullNodeTest, ApplyToStringFails) {
    auto update = fromjson("{$pull : {a: {$lt: 1}}}");
    const CollatorInterface* collator = nullptr;
    PullNode node;
    ASSERT_OK(node.init(update["$pull"]["a"], collator));

    mutablebson::Document doc(fromjson("{a: 'foo'}"));
    setPathTaken("a");
    addIndexedPath("a");
    ASSERT_THROWS_CODE_AND_WHAT(node.apply(getApplyParams(doc.root()["a"])),
                                UserException,
                                ErrorCodes::BadValue,
                                "Cannot apply $pull to a non-array value");
}

TEST_F(PullNodeTest, ApplyToObjectFails) {
    auto update = fromjson("{$pull : {a: {$lt: 1}}}");
    const CollatorInterface* collator = nullptr;
    PullNode node;
    ASSERT_OK(node.init(update["$pull"]["a"], collator));

    mutablebson::Document doc(fromjson("{a: {foo: 'bar'}}"));
    setPathTaken("a");
    addIndexedPath("a");
    ASSERT_THROWS_CODE_AND_WHAT(node.apply(getApplyParams(doc.root()["a"])),
                                UserException,
                                ErrorCodes::BadValue,
                                "Cannot apply $pull to a non-array value");
}

TEST_F(PullNodeTest, ApplyToNonViablePathFails) {
    auto update = fromjson("{$pull : {'a.b': {$lt: 1}}}");
    const CollatorInterface* collator = nullptr;
    PullNode node;
    ASSERT_OK(node.init(update["$pull"]["a.b"], collator));

    mutablebson::Document doc(fromjson("{a: 1}"));
    setPathToCreate("b");
    setPathTaken("a");
    addIndexedPath("a");
    ASSERT_THROWS_CODE_AND_WHAT(
        node.apply(getApplyParams(doc.root()["a"])),
        UserException,
        ErrorCodes::PathNotViable,
        "Cannot use the part (b) of (a.b) to traverse the element ({a: 1})");
}

TEST_F(PullNodeTest, ApplyToMissingElement) {
    auto update = fromjson("{$pull: {'a.b.c.d': {$lt: 1}}}");
    const CollatorInterface* collator = nullptr;
    PullNode node;
    ASSERT_OK(node.init(update["$pull"]["a.b.c.d"], collator));

    mutablebson::Document doc(fromjson("{a: {b: {c: {}}}}"));
    setPathToCreate("d");
    setPathTaken("a.b.c");
    addIndexedPath("a");
    auto result = node.apply(getApplyParams(doc.root()["a"]["b"]["c"]));
    ASSERT_TRUE(result.noop);
    ASSERT_FALSE(result.indexesAffected);
    ASSERT_EQUALS(fromjson("{a: {b: {c: {}}}}"), doc);
    ASSERT_TRUE(doc.isInPlaceModeEnabled());
    ASSERT_EQUALS(fromjson("{}"), getLogDoc());
}

TEST_F(PullNodeTest, ApplyToEmptyArray) {
    auto update = fromjson("{$pull : {a: {$lt: 1}}}");
    const CollatorInterface* collator = nullptr;
    PullNode node;
    ASSERT_OK(node.init(update["$pull"]["a"], collator));

    mutablebson::Document doc(fromjson("{a: []}"));
    setPathTaken("a");
    addIndexedPath("a");
    auto result = node.apply(getApplyParams(doc.root()["a"]));
    ASSERT_TRUE(result.noop);
    ASSERT_FALSE(result.indexesAffected);
    ASSERT_EQUALS(fromjson("{a: []}"), doc);
    ASSERT_TRUE(doc.isInPlaceModeEnabled());
    ASSERT_EQUALS(fromjson("{}"), getLogDoc());
}

TEST_F(PullNodeTest, ApplyToArrayMatchingNone) {
    auto update = fromjson("{$pull : {a: {$lt: 1}}}");
    const CollatorInterface* collator = nullptr;
    PullNode node;
    ASSERT_OK(node.init(update["$pull"]["a"], collator));

    mutablebson::Document doc(fromjson("{a: [2, 3, 4, 5]}"));
    setPathTaken("a");
    addIndexedPath("a");
    auto result = node.apply(getApplyParams(doc.root()["a"]));
    ASSERT_TRUE(result.noop);
    ASSERT_FALSE(result.indexesAffected);
    ASSERT_EQUALS(fromjson("{a: [2, 3, 4, 5]}"), doc);
    ASSERT_TRUE(doc.isInPlaceModeEnabled());
    ASSERT_EQUALS(fromjson("{}"), getLogDoc());
}

TEST_F(PullNodeTest, ApplyToArrayMatchingOne) {
    auto update = fromjson("{$pull : {a: {$lt: 1}}}");
    const CollatorInterface* collator = nullptr;
    PullNode node;
    ASSERT_OK(node.init(update["$pull"]["a"], collator));

    mutablebson::Document doc(fromjson("{a: [0, 1, 2, 3]}"));
    setPathTaken("a");
    addIndexedPath("a");
    auto result = node.apply(getApplyParams(doc.root()["a"]));
    ASSERT_FALSE(result.noop);
    ASSERT_TRUE(result.indexesAffected);
    ASSERT_EQUALS(fromjson("{a: [1, 2, 3]}"), doc);
    ASSERT_FALSE(doc.isInPlaceModeEnabled());
    ASSERT_EQUALS(fromjson("{$set: {a: [1, 2, 3]}}"), getLogDoc());
}

TEST_F(PullNodeTest, ApplyToArrayMatchingSeveral) {
    auto update = fromjson("{$pull : {a: {$lt: 1}}}");
    const CollatorInterface* collator = nullptr;
    PullNode node;
    ASSERT_OK(node.init(update["$pull"]["a"], collator));

    mutablebson::Document doc(fromjson("{a: [0, 1, 0, 2, 0, 3, 0, 4, 0, 5]}"));
    setPathTaken("a");
    addIndexedPath("a");
    auto result = node.apply(getApplyParams(doc.root()["a"]));
    ASSERT_FALSE(result.noop);
    ASSERT_TRUE(result.indexesAffected);
    ASSERT_EQUALS(fromjson("{a: [1, 2, 3, 4, 5]}"), doc);
    ASSERT_FALSE(doc.isInPlaceModeEnabled());
    ASSERT_EQUALS(fromjson("{$set: {a: [1, 2, 3, 4, 5]}}"), getLogDoc());
}

TEST_F(PullNodeTest, ApplyToArrayMatchingAll) {
    auto update = fromjson("{$pull : {a: {$lt: 1}}}");
    const CollatorInterface* collator = nullptr;
    PullNode node;
    ASSERT_OK(node.init(update["$pull"]["a"], collator));

    mutablebson::Document doc(fromjson("{a: [0, -1, -2, -3, -4, -5]}"));
    setPathTaken("a");
    addIndexedPath("a");
    auto result = node.apply(getApplyParams(doc.root()["a"]));
    ASSERT_FALSE(result.noop);
    ASSERT_TRUE(result.indexesAffected);
    ASSERT_EQUALS(fromjson("{a: []}"), doc);
    ASSERT_FALSE(doc.isInPlaceModeEnabled());
    ASSERT_EQUALS(fromjson("{$set: {a: []}}"), getLogDoc());
}

TEST_F(PullNodeTest, ApplyNoIndexDataNoLogBuilder) {
    auto update = fromjson("{$pull : {a: {$lt: 1}}}");
    const CollatorInterface* collator = nullptr;
    PullNode node;
    ASSERT_OK(node.init(update["$pull"]["a"], collator));

    mutablebson::Document doc(fromjson("{a: [0, 1, 2, 3]}"));
    setPathTaken("a");
    setLogBuilderToNull();
    auto result = node.apply(getApplyParams(doc.root()["a"]));
    ASSERT_FALSE(result.noop);
    ASSERT_FALSE(result.indexesAffected);
    ASSERT_EQUALS(fromjson("{a: [1, 2, 3]}"), doc);
    ASSERT_FALSE(doc.isInPlaceModeEnabled());
}

TEST_F(PullNodeTest, ApplyWithCollation) {
    // With the collation, this update will pull any string whose reverse is greater than the
    // reverse of the "abc" string.
    auto update = fromjson("{$pull : {a: {$gt: 'abc'}}}");
    CollatorInterfaceMock collator(CollatorInterfaceMock::MockType::kReverseString);
    PullNode node;
    ASSERT_OK(node.init(update["$pull"]["a"], &collator));

    mutablebson::Document doc(fromjson("{a: ['zaa', 'zcc', 'zbb', 'zee']}"));
    setPathTaken("a");
    addIndexedPath("a");
    auto result = node.apply(getApplyParams(doc.root()["a"]));
    ASSERT_FALSE(result.noop);
    ASSERT_TRUE(result.indexesAffected);
    ASSERT_EQUALS(fromjson("{a: ['zaa', 'zbb']}"), doc);
    ASSERT_FALSE(doc.isInPlaceModeEnabled());
    ASSERT_EQUALS(fromjson("{$set: {a: ['zaa', 'zbb']}}"), getLogDoc());
}

TEST_F(PullNodeTest, ApplyWithCollationDoesNotAffectNonStringMatches) {
    auto update = fromjson("{$pull : {a: {$lt: 1}}}");
    CollatorInterfaceMock collator(CollatorInterfaceMock::MockType::kAlwaysEqual);
    PullNode node;
    ASSERT_OK(node.init(update["$pull"]["a"], &collator));

    mutablebson::Document doc(fromjson("{a: [2, 1, 0, -1, -2, -3]}"));
    setPathTaken("a");
    addIndexedPath("a");
    auto result = node.apply(getApplyParams(doc.root()["a"]));
    ASSERT_FALSE(result.noop);
    ASSERT_TRUE(result.indexesAffected);
    ASSERT_EQUALS(fromjson("{a: [2, 1]}"), doc);
    ASSERT_FALSE(doc.isInPlaceModeEnabled());
    ASSERT_EQUALS(fromjson("{$set: {a: [2, 1]}}"), getLogDoc());
}

TEST_F(PullNodeTest, ApplyWithCollationDoesNotAffectRegexMatches) {
    auto update = fromjson("{$pull : {a: /a/}}");
    CollatorInterfaceMock collator(CollatorInterfaceMock::MockType::kAlwaysEqual);
    PullNode node;
    ASSERT_OK(node.init(update["$pull"]["a"], &collator));

    mutablebson::Document doc(fromjson("{a: ['b', 'a', 'aab', 'cb', 'bba']}"));
    setPathTaken("a");
    addIndexedPath("a");
    auto result = node.apply(getApplyParams(doc.root()["a"]));
    ASSERT_FALSE(result.noop);
    ASSERT_TRUE(result.indexesAffected);
    ASSERT_EQUALS(fromjson("{a: ['b', 'cb']}"), doc);
    ASSERT_FALSE(doc.isInPlaceModeEnabled());
    ASSERT_EQUALS(fromjson("{$set: {a: ['b', 'cb']}}"), getLogDoc());
}

TEST_F(PullNodeTest, ApplyStringLiteralMatchWithCollation) {
    auto update = fromjson("{$pull : {a: 'c'}}");
    CollatorInterfaceMock collator(CollatorInterfaceMock::MockType::kAlwaysEqual);
    PullNode node;
    ASSERT_OK(node.init(update["$pull"]["a"], &collator));

    mutablebson::Document doc(fromjson("{a: ['b', 'a', 'aab', 'cb', 'bba']}"));
    setPathTaken("a");
    addIndexedPath("a");
    auto result = node.apply(getApplyParams(doc.root()["a"]));
    ASSERT_FALSE(result.noop);
    ASSERT_TRUE(result.indexesAffected);
    ASSERT_EQUALS(fromjson("{a: []}"), doc);
    ASSERT_FALSE(doc.isInPlaceModeEnabled());
    ASSERT_EQUALS(fromjson("{$set: {a: []}}"), getLogDoc());
}

TEST_F(PullNodeTest, ApplyCollationDoesNotAffectNumberLiteralMatches) {
    auto update = fromjson("{$pull : {a: 99}}");
    CollatorInterfaceMock collator(CollatorInterfaceMock::MockType::kAlwaysEqual);
    PullNode node;
    ASSERT_OK(node.init(update["$pull"]["a"], &collator));

    mutablebson::Document doc(fromjson("{a: ['a', 99, 'b', 2, 'c', 99, 'd']}"));
    setPathTaken("a");
    addIndexedPath("a");
    auto result = node.apply(getApplyParams(doc.root()["a"]));
    ASSERT_FALSE(result.noop);
    ASSERT_TRUE(result.indexesAffected);
    ASSERT_EQUALS(fromjson("{a: ['a', 'b', 2, 'c', 'd']}"), doc);
    ASSERT_FALSE(doc.isInPlaceModeEnabled());
    ASSERT_EQUALS(fromjson("{$set: {a: ['a', 'b', 2, 'c', 'd']}}"), getLogDoc());
}

TEST_F(PullNodeTest, ApplyStringMatchAfterSetCollator) {
    auto update = fromjson("{$pull : {a: 'c'}}");
    PullNode node;
    const CollatorInterface* collator = nullptr;
    ASSERT_OK(node.init(update["$pull"]["a"], collator));

    // First without a collator.
    mutablebson::Document doc(fromjson("{ a : ['a', 'b', 'c', 'd'] }"));
    setPathTaken("a");
    auto result = node.apply(getApplyParams(doc.root()["a"]));
    ASSERT_FALSE(result.noop);
    ASSERT_FALSE(result.indexesAffected);
    ASSERT_EQUALS(fromjson("{a: ['a', 'b', 'd']}"), doc);
    ASSERT_FALSE(doc.isInPlaceModeEnabled());

    // Now with a collator.
    CollatorInterfaceMock mockCollator(CollatorInterfaceMock::MockType::kAlwaysEqual);
    node.setCollator(&mockCollator);
    mutablebson::Document doc2(fromjson("{ a : ['a', 'b', 'c', 'd'] }"));
    resetApplyParams();
    setPathTaken("a");
    result = node.apply(getApplyParams(doc2.root()["a"]));
    ASSERT_FALSE(result.noop);
    ASSERT_FALSE(result.indexesAffected);
    ASSERT_EQUALS(fromjson("{a: []}"), doc2);
    ASSERT_FALSE(doc2.isInPlaceModeEnabled());
}

TEST_F(PullNodeTest, SetCollatorDoesNotAffectClone) {
    auto update = fromjson("{$pull : {a: 'c'}}");
    PullNode node;
    const CollatorInterface* collator = nullptr;
    ASSERT_OK(node.init(update["$pull"]["a"], collator));

    auto cloneNode = node.clone();

    CollatorInterfaceMock mockCollator(CollatorInterfaceMock::MockType::kAlwaysEqual);
    node.setCollator(&mockCollator);

    // The original node should now have collation.
    mutablebson::Document doc(fromjson("{ a : ['a', 'b', 'c', 'd'] }"));
    setPathTaken("a");
    auto result = node.apply(getApplyParams(doc.root()["a"]));
    ASSERT_FALSE(result.noop);
    ASSERT_FALSE(result.indexesAffected);
    ASSERT_EQUALS(fromjson("{a: []}"), doc);
    ASSERT_FALSE(doc.isInPlaceModeEnabled());

    // The clone should have exact string matches (no collation).
    mutablebson::Document doc2(fromjson("{ a : ['a', 'b', 'c', 'd'] }"));
    resetApplyParams();
    setPathTaken("a");
    result = cloneNode->apply(getApplyParams(doc2.root()["a"]));
    ASSERT_FALSE(result.noop);
    ASSERT_FALSE(result.indexesAffected);
    ASSERT_EQUALS(fromjson("{a: ['a', 'b', 'd']}"), doc2);
    ASSERT_FALSE(doc2.isInPlaceModeEnabled());
}

TEST_F(PullNodeTest, ApplyComplexDocAndMatching1) {
    auto update = fromjson(
        "{$pull: {'a.b': {$or: ["
        "  {'y': {$exists: true }},"
        "  {'z' : {$exists : true}} "
        "]}}}");
    const CollatorInterface* collator = nullptr;
    PullNode node;
    ASSERT_OK(node.init(update["$pull"]["a.b"], collator));

    mutablebson::Document doc(fromjson("{a: {b: [{x: 1}, {y: 'y'}, {x: 2}, {z: 'z'}]}}"));
    setPathTaken("a.b");
    addIndexedPath("a");
    auto result = node.apply(getApplyParams(doc.root()["a"]["b"]));
    ASSERT_FALSE(result.noop);
    ASSERT_TRUE(result.indexesAffected);
    ASSERT_EQUALS(fromjson("{a: {b: [{x: 1}, {x: 2}]}}"), doc);
    ASSERT_FALSE(doc.isInPlaceModeEnabled());
    ASSERT_EQUALS(fromjson("{$set: {'a.b': [{x: 1}, {x: 2}]}}"), getLogDoc());
}

TEST_F(PullNodeTest, ApplyComplexDocAndMatching2) {
    auto update = fromjson("{$pull: {'a.b': {'y': {$exists: true}}}}");
    const CollatorInterface* collator = nullptr;
    PullNode node;
    ASSERT_OK(node.init(update["$pull"]["a.b"], collator));

    mutablebson::Document doc(fromjson("{a: {b: [{x: 1}, {y: 'y'}, {x: 2}, {z: 'z'}]}}"));
    setPathTaken("a.b");
    addIndexedPath("a");
    auto result = node.apply(getApplyParams(doc.root()["a"]["b"]));
    ASSERT_FALSE(result.noop);
    ASSERT_TRUE(result.indexesAffected);
    ASSERT_EQUALS(fromjson("{a: {b: [{x: 1}, {x: 2}, {z: 'z'}]}}"), doc);
    ASSERT_FALSE(doc.isInPlaceModeEnabled());
    ASSERT_EQUALS(fromjson("{$set: {'a.b': [{x: 1}, {x: 2}, {z: 'z'}]}}"), getLogDoc());
}

TEST_F(PullNodeTest, ApplyComplexDocAndMatching3) {
    auto update = fromjson("{$pull: {'a.b': {$in: [{x: 1}, {y: 'y'}]}}}");
    const CollatorInterface* collator = nullptr;
    PullNode node;
    ASSERT_OK(node.init(update["$pull"]["a.b"], collator));

    mutablebson::Document doc(fromjson("{a: {b: [{x: 1}, {y: 'y'}, {x: 2}, {z: 'z'}]}}"));
    setPathTaken("a.b");
    addIndexedPath("a");
    auto result = node.apply(getApplyParams(doc.root()["a"]["b"]));
    ASSERT_FALSE(result.noop);
    ASSERT_TRUE(result.indexesAffected);
    ASSERT_EQUALS(fromjson("{a: {b: [{x: 2}, {z: 'z'}]}}"), doc);
    ASSERT_FALSE(doc.isInPlaceModeEnabled());
    ASSERT_EQUALS(fromjson("{$set: {'a.b': [{x: 2}, {z: 'z'}]}}"), getLogDoc());
}

TEST_F(PullNodeTest, ApplyFullPredicateWithCollation) {
    auto update = fromjson("{$pull: {'a.b': {x: 'blah'}}}");
    CollatorInterfaceMock collator(CollatorInterfaceMock::MockType::kAlwaysEqual);
    PullNode node;
    ASSERT_OK(node.init(update["$pull"]["a.b"], &collator));

    mutablebson::Document doc(
        fromjson("{a: {b: [{x: 'foo', y: 1}, {x: 'bar', y: 2}, {x: 'baz', y: 3}]}}"));
    setPathTaken("a.b");
    addIndexedPath("a");
    auto result = node.apply(getApplyParams(doc.root()["a"]["b"]));
    ASSERT_FALSE(result.noop);
    ASSERT_TRUE(result.indexesAffected);
    ASSERT_EQUALS(fromjson("{a: {b: []}}"), doc);
    ASSERT_FALSE(doc.isInPlaceModeEnabled());
    ASSERT_EQUALS(fromjson("{$set: {'a.b': []}}"), getLogDoc());
}

TEST_F(PullNodeTest, ApplyScalarValueMod) {
    auto update = fromjson("{$pull: {a: 1}}");
    const CollatorInterface* collator = nullptr;
    PullNode node;
    ASSERT_OK(node.init(update["$pull"]["a"], collator));

    mutablebson::Document doc(fromjson("{a: [1, 2, 1, 2, 1, 2]}"));
    setPathTaken("a");
    addIndexedPath("a");
    auto result = node.apply(getApplyParams(doc.root()["a"]));
    ASSERT_FALSE(result.noop);
    ASSERT_TRUE(result.indexesAffected);
    ASSERT_EQUALS(fromjson("{a: [2, 2, 2]}"), doc);
    ASSERT_FALSE(doc.isInPlaceModeEnabled());
    ASSERT_EQUALS(fromjson("{$set: {a: [2, 2, 2]}}"), getLogDoc());
}

TEST_F(PullNodeTest, ApplyObjectValueMod) {
    auto update = fromjson("{$pull: {a: {y: 2}}}");
    const CollatorInterface* collator = nullptr;
    PullNode node;
    ASSERT_OK(node.init(update["$pull"]["a"], collator));

    mutablebson::Document doc(fromjson("{a: [{x: 1}, {y: 2}, {x: 1}, {y: 2}]}"));
    setPathTaken("a");
    addIndexedPath("a");
    auto result = node.apply(getApplyParams(doc.root()["a"]));
    ASSERT_FALSE(result.noop);
    ASSERT_TRUE(result.indexesAffected);
    ASSERT_EQUALS(fromjson("{a: [{x: 1}, {x: 1}]}"), doc);
    ASSERT_FALSE(doc.isInPlaceModeEnabled());
    ASSERT_EQUALS(fromjson("{$set: {a: [{x: 1}, {x: 1}]}}"), getLogDoc());
}

TEST_F(PullNodeTest, DocumentationExample1) {
    auto update = fromjson("{$pull: {flags: 'msr'}}");
    const CollatorInterface* collator = nullptr;
    PullNode node;
    ASSERT_OK(node.init(update["$pull"]["flags"], collator));

    mutablebson::Document doc(
        fromjson("{flags: ['vme', 'de', 'pse', 'tsc', 'msr', 'pae', 'mce']}"));
    setPathTaken("flags");
    addIndexedPath("a");
    auto result = node.apply(getApplyParams(doc.root()["flags"]));
    ASSERT_FALSE(result.noop);
    ASSERT_FALSE(result.indexesAffected);
    ASSERT_EQUALS(fromjson("{flags: ['vme', 'de', 'pse', 'tsc', 'pae', 'mce']}"), doc);
    ASSERT_FALSE(doc.isInPlaceModeEnabled());
    ASSERT_EQUALS(fromjson("{$set: {flags: ['vme', 'de', 'pse', 'tsc', 'pae', 'mce']}}"),
                  getLogDoc());
}

TEST_F(PullNodeTest, DocumentationExample2a) {
    auto update = fromjson("{$pull: {votes: 7}}");
    const CollatorInterface* collator = nullptr;
    PullNode node;
    ASSERT_OK(node.init(update["$pull"]["votes"], collator));

    mutablebson::Document doc(fromjson("{votes: [3, 5, 6, 7, 7, 8]}"));
    setPathTaken("votes");
    addIndexedPath("a");
    auto result = node.apply(getApplyParams(doc.root()["votes"]));
    ASSERT_FALSE(result.noop);
    ASSERT_FALSE(result.indexesAffected);
    ASSERT_EQUALS(fromjson("{votes: [3, 5, 6, 8]}"), doc);
    ASSERT_FALSE(doc.isInPlaceModeEnabled());
    ASSERT_EQUALS(fromjson("{$set: {votes: [3, 5, 6, 8]}}"), getLogDoc());
}

TEST_F(PullNodeTest, DocumentationExample2b) {
    auto update = fromjson("{$pull: {votes: {$gt: 6}}}");
    const CollatorInterface* collator = nullptr;
    PullNode node;
    ASSERT_OK(node.init(update["$pull"]["votes"], collator));

    mutablebson::Document doc(fromjson("{votes: [3, 5, 6, 7, 7, 8]}"));
    setPathTaken("votes");
    addIndexedPath("a");
    auto result = node.apply(getApplyParams(doc.root()["votes"]));
    ASSERT_FALSE(result.noop);
    ASSERT_FALSE(result.indexesAffected);
    ASSERT_EQUALS(fromjson("{votes: [3, 5, 6]}"), doc);
    ASSERT_FALSE(doc.isInPlaceModeEnabled());
    ASSERT_EQUALS(fromjson("{$set: {votes: [3, 5, 6]}}"), getLogDoc());
}

TEST_F(PullNodeTest, ApplyPullWithObjectValueToArrayWithNonObjectValue) {
    auto update = fromjson("{$pull: {a: {x: 1}}}");
    const CollatorInterface* collator = nullptr;
    PullNode node;
    ASSERT_OK(node.init(update["$pull"]["a"], collator));

    mutablebson::Document doc(fromjson("{a: [{x: 1}, 2]}"));
    setPathTaken("a");
    addIndexedPath("a");
    auto result = node.apply(getApplyParams(doc.root()["a"]));
    ASSERT_FALSE(result.noop);
    ASSERT_TRUE(result.indexesAffected);
    ASSERT_EQUALS(fromjson("{a: [2]}"), doc);
    ASSERT_FALSE(doc.isInPlaceModeEnabled());
    ASSERT_EQUALS(fromjson("{$set: {a: [2]}}"), getLogDoc());
}

TEST_F(PullNodeTest, CannotModifyImmutableField) {
    auto update = fromjson("{$pull: {'_id.a': 1}}");
    const CollatorInterface* collator = nullptr;
    PullNode node;
    ASSERT_OK(node.init(update["$pull"]["_id.a"], collator));

    mutablebson::Document doc(fromjson("{_id: {a: [0, 1, 2]}}"));
    setPathTaken("_id.a");
    addImmutablePath("_id");
    ASSERT_THROWS_CODE_AND_WHAT(
        node.apply(getApplyParams(doc.root()["_id"]["a"])),
        UserException,
        ErrorCodes::ImmutableField,
        "Performing an update on the path '_id.a' would modify the immutable field '_id'");
}

TEST_F(PullNodeTest, SERVER_3988) {
    auto update = fromjson("{$pull: {y: /yz/}}");
    const CollatorInterface* collator = nullptr;
    PullNode node;
    ASSERT_OK(node.init(update["$pull"]["y"], collator));

    mutablebson::Document doc(fromjson("{x: 1, y: [2, 3, 4, 'abc', 'xyz']}"));
    setPathTaken("y");
    addIndexedPath("a");
    auto result = node.apply(getApplyParams(doc.root()["y"]));
    ASSERT_FALSE(result.noop);
    ASSERT_FALSE(result.indexesAffected);
    ASSERT_EQUALS(fromjson("{x: 1, y: [2, 3, 4, 'abc']}"), doc);
    ASSERT_FALSE(doc.isInPlaceModeEnabled());
    ASSERT_EQUALS(fromjson("{$set: {y: [2, 3, 4, 'abc']}}"), getLogDoc());
}

}  // namespace
}  // namespace mongo