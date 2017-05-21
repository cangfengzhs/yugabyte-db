//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#include <thread>
#include <cmath>

#include "yb/sql/test/ybsql-test-base.h"
#include "yb/gutil/strings/substitute.h"

using std::string;
using std::unique_ptr;
using std::shared_ptr;
using strings::Substitute;

namespace yb {
namespace sql {

class YbSqlQuery : public YbSqlTestBase {
 public:
  YbSqlQuery() : YbSqlTestBase() {
  }

  std::shared_ptr<YQLRowBlock> ExecSelect(YbSqlProcessor *processor, int expected_rows = 1) {
    auto select = "SELECT c1, c2, c3 FROM test_table WHERE c1 = 1";
    Status s = processor->Run(select);
    CHECK(s.ok());
    auto row_block = processor->row_block();
    EXPECT_EQ(expected_rows, row_block->row_count());
    return row_block;
  }

  void VerifyExpiry(YbSqlProcessor *processor) {
    ExecSelect(processor, 0);
  }

  void CreateTableAndInsertRow(YbSqlProcessor *processor, bool with_ttl = true) {
    // Create the table.
    const char *create_stmt =
        "CREATE TABLE test_table(c1 int, c2 int, c3 int, "
            "primary key(c1));";
    Status s = processor->Run(create_stmt);
    CHECK(s.ok());

    std::string insert_stmt("INSERT INTO test_table(c1, c2, c3) VALUES(1, 2, 3)");
    if (with_ttl) {
      // Insert row with ttl.
      insert_stmt += " USING TTL 1;";
    } else {
      insert_stmt += ";";
    }
    s = processor->Run(insert_stmt);
    CHECK_OK(s);

    // Verify row is present.
    auto row_block = ExecSelect(processor);
    YQLRow& row = row_block->row(0);

    EXPECT_EQ(1, row.column(0).int32_value());
    EXPECT_EQ(2, row.column(1).int32_value());
    EXPECT_EQ(3, row.column(2).int32_value());
  }

};

TEST_F(YbSqlQuery, TestMissingSystemTable) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  YbSqlProcessor *processor = GetSqlProcessor();
  const char* statement = "SELECT * FROM system.invalid_system_table_name";
  constexpr auto kRepetitions = 10;
  for (auto i = 0; i != kRepetitions; ++i) {
    CHECK_VALID_STMT(statement);
  }
}

TEST_F(YbSqlQuery, TestSqlQuerySimple) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  YbSqlProcessor *processor = GetSqlProcessor();
  LOG(INFO) << "Running simple query test.";
  // Create the table 1.
  const char *create_stmt =
    "CREATE TABLE test_table(h1 int, h2 varchar, "
                            "r1 int, r2 varchar, "
                            "v1 int, v2 varchar, "
                            "primary key((h1, h2), r1, r2));";
  CHECK_VALID_STMT(create_stmt);

  // Test NOTFOUND. Select from empty table.
  CHECK_VALID_STMT("SELECT * FROM test_table");
  std::shared_ptr<YQLRowBlock> empty_row_block = processor->row_block();
  CHECK_EQ(empty_row_block->row_count(), 0);
  CHECK_VALID_STMT("SELECT * FROM test_table WHERE h1 = 0 AND h2 = ''");
  empty_row_block = processor->row_block();
  CHECK_EQ(empty_row_block->row_count(), 0);

  // Check for valid allow filtering clause.
  CHECK_VALID_STMT("SELECT * FROM test_table WHERE h1 = 0 AND h2 = '' ALLOW FILTERING");
  empty_row_block = processor->row_block();
  CHECK_EQ(empty_row_block->row_count(), 0);

  // Insert 100 rows into the table.
  static const int kNumRows = 100;
  for (int idx = 0; idx < kNumRows; idx++) {
    // INSERT: Valid statement with column list.
    string stmt = Substitute("INSERT INTO test_table(h1, h2, r1, r2, v1, v2) "
                             "VALUES($0, 'h$1', $2, 'r$3', $4, 'v$5');",
                             idx, idx, idx+100, idx+100, idx+1000, idx+1000);
    CHECK_VALID_STMT(stmt);
  }
  LOG(INFO) << kNumRows << " rows inserted";

  //------------------------------------------------------------------------------------------------
  // Basic negative cases.
  // Test simple query and result.
  CHECK_INVALID_STMT("SELECT h1, h2, r1, r2, v1, v2 FROM test_table "
                     "  WHERE h1 = 7 AND h2 = 'h7' AND v1 = 1007;");
  CHECK_INVALID_STMT("SELECT h1, h2, r1, r2, v1, v2 FROM test_table "
                     "  WHERE h1 = 7 AND h2 = 'h7' AND v1 = 100;");

  //------------------------------------------------------------------------------------------------
  // Test simple query and result.
  CHECK_VALID_STMT("SELECT h1, h2, r1, r2, v1, v2 FROM test_table "
                   "  WHERE h1 = 7 AND h2 = 'h7' AND r1 = 107;");

  std::shared_ptr<YQLRowBlock> row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  const YQLRow& ordered_row = row_block->row(0);
  CHECK_EQ(ordered_row.column(0).int32_value(), 7);
  CHECK_EQ(ordered_row.column(1).string_value(), "h7");
  CHECK_EQ(ordered_row.column(2).int32_value(), 107);
  CHECK_EQ(ordered_row.column(3).string_value(), "r107");
  CHECK_EQ(ordered_row.column(4).int32_value(), 1007);
  CHECK_EQ(ordered_row.column(5).string_value(), "v1007");

  // Test simple query and result with different order.
  CHECK_VALID_STMT("SELECT v1, v2, h1, h2, r1, r2 FROM test_table "
                   "  WHERE h1 = 7 AND h2 = 'h7' AND r1 = 107;");

  row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  const YQLRow& unordered_row = row_block->row(0);
  CHECK_EQ(unordered_row.column(0).int32_value(), 1007);
  CHECK_EQ(unordered_row.column(1).string_value(), "v1007");
  CHECK_EQ(unordered_row.column(2).int32_value(), 7);
  CHECK_EQ(unordered_row.column(3).string_value(), "h7");
  CHECK_EQ(unordered_row.column(4).int32_value(), 107);
  CHECK_EQ(unordered_row.column(5).string_value(), "r107");

  // Test single row query for the whole table.
  for (int idx = 0; idx < kNumRows; idx++) {
    // SELECT: Valid statement with column list.
    string stmt = Substitute("SELECT h1, h2, r1, r2, v1, v2 FROM test_table "
                             "WHERE h1 = $0 AND h2 = 'h$1' AND r1 = $2 AND r2 = 'r$3';",
                             idx, idx, idx+100, idx+100);
    CHECK_VALID_STMT(stmt);

    row_block = processor->row_block();
    CHECK_EQ(row_block->row_count(), 1);
    const YQLRow& row = row_block->row(0);
    CHECK_EQ(row.column(0).int32_value(), idx);
    CHECK_EQ(row.column(1).string_value(), Substitute("h$0", idx));
    CHECK_EQ(row.column(2).int32_value(), idx + 100);
    CHECK_EQ(row.column(3).string_value(), Substitute("r$0", idx + 100));
    CHECK_EQ(row.column(4).int32_value(), idx + 1000);
    CHECK_EQ(row.column(5).string_value(), Substitute("v$0", idx + 1000));
  }

  // Test multi row query for the whole table.
  // Insert 20 rows of the same hash key into the table.
  static const int kHashNumRows = 20;
  int32 h1_shared = 1111111;
  const string h2_shared = "h2_shared_key";
  for (int idx = 0; idx < kHashNumRows; idx++) {
    // INSERT: Valid statement with column list.
    string stmt = Substitute("INSERT INTO test_table(h1, h2, r1, r2, v1, v2) "
                             "VALUES($0, '$1', $2, 'r$3', $4, 'v$5');",
                             h1_shared, h2_shared, idx+100, idx+100, idx+1000, idx+1000);
    CHECK_VALID_STMT(stmt);
  }

  // Select all 20 rows and check the values.
  const string multi_select = Substitute("SELECT h1, h2, r1, r2, v1, v2 FROM test_table "
                                         "WHERE h1 = $0 AND h2 = '$1';",
                                         h1_shared, h2_shared);
  CHECK_VALID_STMT(multi_select);
  row_block = processor->row_block();

  // Check the result set.
  CHECK_EQ(row_block->row_count(), kHashNumRows);
  for (int idx = 0; idx < kHashNumRows; idx++) {
    const YQLRow& row = row_block->row(idx);
    CHECK_EQ(row.column(0).int32_value(), h1_shared);
    CHECK_EQ(row.column(1).string_value(), h2_shared);
    CHECK_EQ(row.column(2).int32_value(), idx + 100);
    CHECK_EQ(row.column(3).string_value(), Substitute("r$0", idx + 100));
    CHECK_EQ(row.column(4).int32_value(), idx + 1000);
    CHECK_EQ(row.column(5).string_value(), Substitute("v$0", idx + 1000));
  }

  // Select only 2 rows and check the values.
  int limit = 2;
  string limit_select = Substitute("SELECT h1, h2, r1, r2, v1, v2 FROM test_table "
                                   "WHERE h1 = $0 AND h2 = '$1' LIMIT $2;",
                                   h1_shared, h2_shared, limit);
  CHECK_VALID_STMT(limit_select);
  row_block = processor->row_block();

  // Check the result set.
  CHECK_EQ(row_block->row_count(), limit);
  int32_t prev_r1 = 0;
  string prev_r2;
  for (int idx = 0; idx < limit; idx++) {
    const YQLRow& row = row_block->row(idx);
    CHECK_EQ(row.column(0).int32_value(), h1_shared);
    CHECK_EQ(row.column(1).string_value(), h2_shared);
    CHECK_EQ(row.column(2).int32_value(), idx + 100);
    CHECK_EQ(row.column(3).string_value(), Substitute("r$0", idx + 100));
    CHECK_EQ(row.column(4).int32_value(), idx + 1000);
    CHECK_EQ(row.column(5).string_value(), Substitute("v$0", idx + 1000));
    CHECK_GT(row.column(2).int32_value(), prev_r1);
    CHECK_GT(row.column(3).string_value(), prev_r2);
    prev_r1 = row.column(2).int32_value();
    prev_r2 = row.column(3).string_value();
  }

  limit_select = Substitute("SELECT h1, h2, r1, r2, v1, v2 FROM test_table "
                            "WHERE h1 = $0 AND h2 = '$1' LIMIT $2 ALLOW FILTERING;",
                            h1_shared, h2_shared, limit);
  CHECK_VALID_STMT(limit_select);

  const string drop_stmt = "DROP TABLE test_table;";
  EXEC_VALID_STMT(drop_stmt);
}

TEST_F(YbSqlQuery, TestPagingState) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  YbSqlProcessor *processor = GetSqlProcessor();

  LOG(INFO) << "Running paging state test.";

  // Create table.
  CHECK_VALID_STMT("CREATE TABLE t (h int, r int, v int, primary key((h), r));");

  static constexpr int kNumRows = 100;
  // Insert 100 rows of the same hash key into the table.
  {
    for (int i = 1; i <= kNumRows; i++) {
      // INSERT: Valid statement with column list.
      string stmt = Substitute("INSERT INTO t (h, r, v) VALUES ($0, $1, $2);", 1, i, 100 + i);
      CHECK_VALID_STMT(stmt);
    }
    LOG(INFO) << kNumRows << " rows inserted";
  }

  // Read a single row. Verify row and that the paging state is empty.
  CHECK_VALID_STMT("SELECT h, r, v FROM t WHERE h = 1 AND r = 1;");
  std::shared_ptr<YQLRowBlock> row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  const YQLRow& row = row_block->row(0);
  CHECK_EQ(row.column(0).int32_value(), 1);
  CHECK_EQ(row.column(1).int32_value(), 1);
  CHECK_EQ(row.column(2).int32_value(), 101);
  CHECK(processor->rows_result()->paging_state().empty());

  // Read all rows. Verify rows and that they are read in the number of pages expected.
  {
    StatementParameters params;
    int kPageSize = 5;
    params.set_page_size(kPageSize);
    int page_count = 0;
    int i = 0;
    do {
      CHECK_OK(processor->Run("SELECT h, r, v FROM t WHERE h = 1;", params));
      std::shared_ptr<YQLRowBlock> row_block = processor->row_block();
      CHECK_EQ(row_block->row_count(), kPageSize);
      for (int j = 0; j < kPageSize; j++) {
        const YQLRow& row = row_block->row(j);
        i++;
        CHECK_EQ(row.column(0).int32_value(), 1);
        CHECK_EQ(row.column(1).int32_value(), i);
        CHECK_EQ(row.column(2).int32_value(), 100 + i);
      }
      page_count++;
      if (processor->rows_result()->paging_state().empty()) {
        break;
      }
      CHECK_OK(params.set_paging_state(processor->rows_result()->paging_state()));
    } while (true);
    CHECK_EQ(page_count, kNumRows / kPageSize);
  }

  // Read rows with a LIMIT. Verify rows and that they are read in the number of pages expected.
  {
    StatementParameters params;
    static constexpr int kLimit = 53;
    static constexpr int kPageSize = 5;
    params.set_page_size(kPageSize);
    int page_count = 0;
    int i = 0;
    string select_stmt = Substitute("SELECT h, r, v FROM t WHERE h = 1 LIMIT $0;", kLimit);
    do {
      CHECK_OK(processor->Run(select_stmt, params));
      std::shared_ptr<YQLRowBlock> row_block = processor->row_block();
      for (int j = 0; j < row_block->row_count(); j++) {
        const YQLRow& row = row_block->row(j);
        i++;
        CHECK_EQ(row.column(0).int32_value(), 1);
        CHECK_EQ(row.column(1).int32_value(), i);
        CHECK_EQ(row.column(2).int32_value(), 100 + i);
      }
      page_count++;
      if (processor->rows_result()->paging_state().empty()) {
        break;
      }
      CHECK_EQ(row_block->row_count(), kPageSize);
      CHECK_OK(params.set_paging_state(processor->rows_result()->paging_state()));
    } while (true);
    CHECK_EQ(i, kLimit);
    CHECK_EQ(page_count, static_cast<int>(ceil(static_cast<double>(kLimit) /
                                               static_cast<double>(kPageSize))));
  }

  // Insert anther 100 rows of different hash keys into the table.
  {
    for (int i = 1; i <= kNumRows; i++) {
      // INSERT: Valid statement with column list.
      string stmt = Substitute("INSERT INTO t (h, r, v) VALUES ($0, $1, $2);", i, 100 + i, 200 + i);
      CHECK_VALID_STMT(stmt);
    }
    LOG(INFO) << kNumRows << " rows inserted";
  }

  // Test full-table query without a hash key.

  // Read all rows. Verify rows and that they are read in the number of pages expected.
  {
    StatementParameters params;
    int kPageSize = 5;
    params.set_page_size(kPageSize);
    int page_count = 0;
    int row_count = 0;
    int sum = 0;
    do {
      CHECK_OK(processor->Run("SELECT h, r, v FROM t WHERE r > 100;", params));
      std::shared_ptr<YQLRowBlock> row_block = processor->row_block();
      for (int j = 0; j < row_block->row_count(); j++) {
        const YQLRow& row = row_block->row(j);
        CHECK_EQ(row.column(0).int32_value() + 100, row.column(1).int32_value());
        sum += row.column(0).int32_value();
        row_count++;
      }
      page_count++;
      if (processor->rows_result()->paging_state().empty()) {
        break;
      }
      CHECK_OK(params.set_paging_state(processor->rows_result()->paging_state()));
    } while (true);
    CHECK_EQ(row_count, kNumRows);
    // Page count should be at least "kNumRows / kPageSize". Can be more because some pages may not
    // be fully filled depending on the hash key distribution.
    CHECK_GE(page_count, kNumRows / kPageSize);
    CHECK_EQ(sum, (1 + kNumRows) * kNumRows / 2);
  }

  // Read rows with a LIMIT. Verify rows and that they are read in the number of pages expected.
  {
    StatementParameters params;
    static constexpr int kLimit = 53;
    static constexpr int kPageSize = 5;
    params.set_page_size(kPageSize);
    int page_count = 0;
    int row_count = 0;
    int sum = 0;
    string select_stmt = Substitute("SELECT h, r, v FROM t WHERE r > 100 LIMIT $0;", kLimit);
    do {
      CHECK_OK(processor->Run(select_stmt, params));
      std::shared_ptr<YQLRowBlock> row_block = processor->row_block();
      for (int j = 0; j < row_block->row_count(); j++) {
        const YQLRow& row = row_block->row(j);
        CHECK_EQ(row.column(0).int32_value() + 100, row.column(1).int32_value());
        sum += row.column(0).int32_value();
        row_count++;
      }
      page_count++;
      if (processor->rows_result()->paging_state().empty()) {
        break;
      }
      CHECK_OK(params.set_paging_state(processor->rows_result()->paging_state()));
    } while (true);
    CHECK_EQ(row_count, kLimit);
    // Page count should be at least "kLimit / kPageSize". Can be more because some pages may not
    // be fully filled depending on the hash key distribution. Same for sum which should be at
    // least the sum of the lowest consecutive kLimit number of "h" values. Can be more.
    CHECK_GE(page_count, static_cast<int>(ceil(static_cast<double>(kLimit) /
                                               static_cast<double>(kPageSize))));
    CHECK_GE(sum, (1 + kLimit) * kLimit / 2);
  }
}

TEST_F(YbSqlQuery, TestSqlQueryPartialHash) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  YbSqlProcessor *processor = GetSqlProcessor();

  LOG(INFO) << "Running partial hash test.";
  // Create the table 1.
  const char *create_stmt =
    "CREATE TABLE test_table(h1 int, h2 varchar, "
                            "h3 bigint, h4 varchar, "
                            "r1 int, r2 varchar, "
                            "v1 int, v2 varchar, "
                            "primary key((h1, h2, h3, h4), r1, r2));";
  CHECK_VALID_STMT(create_stmt);

  // Test NOTFOUND. Select from empty table.
  CHECK_VALID_STMT("SELECT * FROM test_table");
  std::shared_ptr<YQLRowBlock> empty_row_block = processor->row_block();
  CHECK_EQ(empty_row_block->row_count(), 0);
  CHECK_VALID_STMT("SELECT * FROM test_table WHERE h1 = 0 AND h2 = ''");
  empty_row_block = processor->row_block();
  CHECK_EQ(empty_row_block->row_count(), 0);

  // Insert 100 rows into the table.
  static const int kNumRows = 100;
  for (int idx = 0; idx < kNumRows; idx++) {
    // INSERT: Valid statement with column list.
    string stmt = Substitute("INSERT INTO test_table(h1, h2, h3, h4, r1, r2, v1, v2) "
                             "VALUES($0, 'h$1', $2, 'h$3', $4, 'r$5', $6, 'v$7');",
                             idx, idx, idx+100, idx+100, idx+1000, idx+1000, idx+10000, idx+10000);
    CHECK_VALID_STMT(stmt);
  }
  LOG(INFO) << kNumRows << " rows inserted";

  //------------------------------------------------------------------------------------------------
  // Basic negative cases.
  // Test simple query and result.
  CHECK_INVALID_STMT("SELECT h1, h2, h3, h4, r1, r2, v1, v2 FROM test_table "
                     "  WHERE h1 = 7 AND h2 = 'h7' AND v1 = 10007;");
  CHECK_INVALID_STMT("SELECT h1, h2, h3, h4, r1, r2, v1, v2 FROM test_table "
                     "  WHERE h1 = 7 AND h2 = 'h7' AND v2 = 'v10007';");

  //------------------------------------------------------------------------------------------------
  // Check invalid case for using other operators for hash keys.
  CHECK_INVALID_STMT("SELECT h1, h2, h3, h4, r1, r2, v1, v2 FROM test_table "
                     "  WHERE h1 < 7;");
  CHECK_INVALID_STMT("SELECT h1, h2, h3, h4, r1, r2, v1, v2 FROM test_table "
                     "  WHERE h1 > 7 AND h2 > 'h7';");


  //------------------------------------------------------------------------------------------------
  // Test partial hash keys and results.
  LOG(INFO) << "Testing 3 out of 4 keys";
  CHECK_VALID_STMT("SELECT h1, h2, h3, h4, r1, r2, v1, v2 FROM test_table "
                   "  WHERE h1 = 7 AND h2 = 'h7' AND h3 = 107;");
  std::shared_ptr<YQLRowBlock> row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  const YQLRow& no_hash_row1 = row_block->row(0);
  CHECK_EQ(no_hash_row1.column(0).int32_value(), 7);
  CHECK_EQ(no_hash_row1.column(1).string_value(), "h7");
  CHECK_EQ(no_hash_row1.column(2).int64_value(), 107);
  CHECK_EQ(no_hash_row1.column(3).string_value(), "h107");
  CHECK_EQ(no_hash_row1.column(4).int32_value(), 1007);
  CHECK_EQ(no_hash_row1.column(5).string_value(), "r1007");
  CHECK_EQ(no_hash_row1.column(6).int32_value(), 10007);
  CHECK_EQ(no_hash_row1.column(7).string_value(), "v10007");

  LOG(INFO) << "Testing 2 out of 4 keys";
  CHECK_VALID_STMT("SELECT h1, h2, h3, h4, r1, r2, v1, v2 FROM test_table "
                   "  WHERE h1 = 7 AND h2 = 'h7';");
  row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  const YQLRow& no_hash_row2 = row_block->row(0);
  CHECK_EQ(no_hash_row2.column(0).int32_value(), 7);
  CHECK_EQ(no_hash_row2.column(1).string_value(), "h7");
  CHECK_EQ(no_hash_row2.column(2).int64_value(), 107);
  CHECK_EQ(no_hash_row2.column(3).string_value(), "h107");
  CHECK_EQ(no_hash_row2.column(4).int32_value(), 1007);
  CHECK_EQ(no_hash_row2.column(5).string_value(), "r1007");
  CHECK_EQ(no_hash_row2.column(6).int32_value(), 10007);
  CHECK_EQ(no_hash_row2.column(7).string_value(), "v10007");

  LOG(INFO) << "Testing 1 out of 4 keys";
  CHECK_VALID_STMT("SELECT h1, h2, h3, h4, r1, r2, v1, v2 FROM test_table "
                   "  WHERE h1 = 7;");
  row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  const YQLRow& no_hash_row3 = row_block->row(0);
  CHECK_EQ(no_hash_row3.column(0).int32_value(), 7);
  CHECK_EQ(no_hash_row3.column(1).string_value(), "h7");
  CHECK_EQ(no_hash_row3.column(2).int64_value(), 107);
  CHECK_EQ(no_hash_row3.column(3).string_value(), "h107");
  CHECK_EQ(no_hash_row3.column(4).int32_value(), 1007);
  CHECK_EQ(no_hash_row3.column(5).string_value(), "r1007");
  CHECK_EQ(no_hash_row3.column(6).int32_value(), 10007);
  CHECK_EQ(no_hash_row3.column(7).string_value(), "v10007");

  // Test simple query with only range key and check result.
  LOG(INFO) << "Testing 0 out of 4 keys";
  CHECK_VALID_STMT("SELECT h1, h2, h3, h4, r1, r2, v1, v2 FROM test_table "
                   "WHERE r1 = 1007;");
  row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  const YQLRow& no_hash_row4 = row_block->row(0);
  CHECK_EQ(no_hash_row4.column(0).int32_value(), 7);
  CHECK_EQ(no_hash_row4.column(1).string_value(), "h7");
  CHECK_EQ(no_hash_row4.column(2).int64_value(), 107);
  CHECK_EQ(no_hash_row4.column(3).string_value(), "h107");
  CHECK_EQ(no_hash_row4.column(4).int32_value(), 1007);
  CHECK_EQ(no_hash_row4.column(5).string_value(), "r1007");
  CHECK_EQ(no_hash_row4.column(6).int32_value(), 10007);
  CHECK_EQ(no_hash_row4.column(7).string_value(), "v10007");

  LOG(INFO) << "Testing 1 of every key each.";
  // Test simple query with partial hash key and check result.
  CHECK_VALID_STMT("SELECT h1, h2, h3, h4, r1, r2, v1, v2 FROM test_table "
                   "WHERE h1 = 7;");
  row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  const YQLRow& h1_hash_row = row_block->row(0);
  CHECK_EQ(h1_hash_row.column(0).int32_value(), 7);
  CHECK_EQ(h1_hash_row.column(1).string_value(), "h7");
  CHECK_EQ(h1_hash_row.column(2).int64_value(), 107);
  CHECK_EQ(h1_hash_row.column(3).string_value(), "h107");
  CHECK_EQ(h1_hash_row.column(4).int32_value(), 1007);
  CHECK_EQ(h1_hash_row.column(5).string_value(), "r1007");
  CHECK_EQ(h1_hash_row.column(6).int32_value(), 10007);
  CHECK_EQ(h1_hash_row.column(7).string_value(), "v10007");

  CHECK_VALID_STMT("SELECT h1, h2, h3, h4, r1, r2, v1, v2 FROM test_table "
                   "  WHERE h2 = 'h7';");
  row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  const YQLRow& h2_hash_row = row_block->row(0);
  CHECK_EQ(h2_hash_row.column(0).int32_value(), 7);
  CHECK_EQ(h2_hash_row.column(1).string_value(), "h7");
  CHECK_EQ(h2_hash_row.column(2).int64_value(), 107);
  CHECK_EQ(h2_hash_row.column(3).string_value(), "h107");
  CHECK_EQ(h2_hash_row.column(4).int32_value(), 1007);
  CHECK_EQ(h2_hash_row.column(5).string_value(), "r1007");
  CHECK_EQ(h2_hash_row.column(6).int32_value(), 10007);
  CHECK_EQ(h2_hash_row.column(7).string_value(), "v10007");

  CHECK_VALID_STMT("SELECT h1, h2, h3, h4, r1, r2, v1, v2 FROM test_table "
                   "  WHERE h3 = 107;");
  row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  const YQLRow& h3_hash_row = row_block->row(0);
  CHECK_EQ(h3_hash_row.column(0).int32_value(), 7);
  CHECK_EQ(h3_hash_row.column(1).string_value(), "h7");
  CHECK_EQ(h3_hash_row.column(2).int64_value(), 107);
  CHECK_EQ(h3_hash_row.column(3).string_value(), "h107");
  CHECK_EQ(h3_hash_row.column(4).int32_value(), 1007);
  CHECK_EQ(h3_hash_row.column(5).string_value(), "r1007");
  CHECK_EQ(h3_hash_row.column(6).int32_value(), 10007);
  CHECK_EQ(h3_hash_row.column(7).string_value(), "v10007");

  CHECK_VALID_STMT("SELECT h1, h2, h3, h4, r1, r2, v1, v2 FROM test_table "
                   "  WHERE h4 = 'h107';");
  row_block = processor->row_block();
  CHECK_EQ(row_block->row_count(), 1);
  const YQLRow& h4_hash_row = row_block->row(0);
  CHECK_EQ(h4_hash_row.column(0).int32_value(), 7);
  CHECK_EQ(h4_hash_row.column(1).string_value(), "h7");
  CHECK_EQ(h4_hash_row.column(2).int64_value(), 107);
  CHECK_EQ(h4_hash_row.column(3).string_value(), "h107");
  CHECK_EQ(h4_hash_row.column(4).int32_value(), 1007);
  CHECK_EQ(h4_hash_row.column(5).string_value(), "r1007");
  CHECK_EQ(h4_hash_row.column(6).int32_value(), 10007);
  CHECK_EQ(h4_hash_row.column(7).string_value(), "v10007");


  // Test multi row query for the whole table.
  // Insert 20 rows of the same hash key into the table.
  static const int kHashNumRows = 20;
  static const int kNumFilterRows = 10;
  int32 h1_shared = 1111111;
  const string h2_shared = "h2_shared_key";
  int64 h3_shared = 111111111;
  const string h4_shared = "h4_shared_key";
  for (int idx = 0; idx < kHashNumRows; idx++) {
    // INSERT: Valid statement with column list.
    string stmt = Substitute("INSERT INTO test_table(h1, h2, h3, h4, r1, r2, v1, v2) "
                             "VALUES($0, '$1', $2, '$3', $4, 'r$5', $6, 'v$7');",
                             h1_shared, h2_shared, h3_shared, h4_shared,
                             idx+100, idx+100, idx+1000, idx+1000);
    CHECK_VALID_STMT(stmt);
  }

  // Select select rows and check the values.
  // This test scans multiple tservers. Query result tested in java.
  // TODO: Make YBSQL understand paging states and continue.
  LOG(INFO) << "Testing filter with partial hash keys.";
  const string multi_select = Substitute("SELECT h1, h2, h3, h4, r1, r2, v1, v2 FROM test_table "
                                         "WHERE h1 = $0 AND h2 = '$1' AND r1 > $2;",
                                         h1_shared, h2_shared, kNumFilterRows + 100);
  CHECK_VALID_STMT(multi_select);

  const string drop_stmt = "DROP TABLE test_table;";
  EXEC_VALID_STMT(drop_stmt);
}

TEST_F(YbSqlQuery, TestInsertWithTTL) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  YbSqlProcessor *processor = GetSqlProcessor();

  CreateTableAndInsertRow(processor);

  // Sleep for 1.1 seconds and verify ttl has expired.
  std::this_thread::sleep_for(std::chrono::milliseconds(1100));

  VerifyExpiry(processor);
}

TEST_F(YbSqlQuery, TestUpdateWithTTL) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  YbSqlProcessor *processor = GetSqlProcessor();

  CreateTableAndInsertRow(processor, false);

  // Now update the row with a TTL.
  std::string update_stmt("UPDATE test_table USING TTL 1 SET c2 = 4, c3 = 5 WHERE c1 = 1;");
  Status s = processor->Run(update_stmt);
  CHECK(s.ok());

  // Sleep for 1.1 seconds and verify ttl has expired.
  std::this_thread::sleep_for(std::chrono::milliseconds(1100));

  // c1 = 1 should still exist.
  auto row_block = ExecSelect(processor);
  YQLRow& row = row_block->row(0);

  EXPECT_EQ(1, row.column(0).int32_value());
  EXPECT_TRUE(row.column(1).IsNull());
  EXPECT_TRUE(row.column(2).IsNull());

  // Try an update by setting the primary key, which should fail since set clause can't have
  // primary keys.
  std::string invalid_update_stmt("UPDATE test_table USING TTL 1 SET c1 = 4 WHERE c1 = 1;");
  s = processor->Run(invalid_update_stmt);
  CHECK(!s.ok());
}

// The main goal of this test is to check that the serialization/deserialization operations match
// The Java tests are more comprehensive but do not test the deserialization -- since they use the
// Cassandra deserializer instead
TEST_F(YbSqlQuery, TestCollectionTypes) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  YbSqlProcessor *processor = GetSqlProcessor();

  //------------------------------------------------------------------------------------------------
  // Testing Map type
  //------------------------------------------------------------------------------------------------

  // Create table.
  const char *map_create_stmt =
      "CREATE TABLE map_test (id int PRIMARY KEY, v int, mp map<int, varchar>, c varchar);";
  Status s = processor->Run(map_create_stmt);
  CHECK(s.ok());

  // Insert Values
  std::string map_insert_stmt("INSERT INTO map_test (id, v, mp, c) values "
      "(1, 3, {21 : 'a', 22 : 'b', 23 : 'c'}, 'x');");
  s = processor->Run(map_insert_stmt);
  CHECK_OK(s);

  // Check Select
  auto map_select_stmt = "SELECT * FROM map_test WHERE id = 1";
  s = processor->Run(map_select_stmt);
  CHECK(s.ok());
  auto map_row_block = processor->row_block();
  EXPECT_EQ(1, map_row_block->row_count());
  YQLRow& map_row = map_row_block->row(0);

  // check row
  EXPECT_EQ(1, map_row.column(0).int32_value());
  EXPECT_EQ(3, map_row.column(1).int32_value());
  EXPECT_EQ("x", map_row.column(3).string_value());
  // check map
  EXPECT_EQ(YQLValue::InternalType::kMapValue, map_row.column(2).type());
  YQLMapValuePB map_value = map_row.column(2).map_value();
  // check keys
  EXPECT_EQ(3, map_value.keys_size());
  EXPECT_EQ(21, map_value.keys(0).int32_value());
  EXPECT_EQ(22, map_value.keys(1).int32_value());
  EXPECT_EQ(23, map_value.keys(2).int32_value());
  // check values
  EXPECT_EQ(3, map_value.values_size());
  EXPECT_EQ("a", map_value.values(0).string_value());
  EXPECT_EQ("b", map_value.values(1).string_value());
  EXPECT_EQ("c", map_value.values(2).string_value());

  //------------------------------------------------------------------------------------------------
  // Testing Set type
  //------------------------------------------------------------------------------------------------

  // Create table.
  const char *set_create_stmt =
      "CREATE TABLE set_test (id int PRIMARY KEY, v int, st set<int>, c varchar);";
  s = processor->Run(set_create_stmt);
  CHECK(s.ok());

  // Insert Values
  std::string set_insert_stmt("INSERT INTO set_test (id, v, st, c) values "
      "(1, 3, {3, 4, 1, 1, 2, 4, 2}, 'x');");
  s = processor->Run(set_insert_stmt);
  CHECK_OK(s);

  // Check Select
  auto set_select_stmt = "SELECT * FROM set_test WHERE id = 1";
  s = processor->Run(set_select_stmt);
  CHECK(s.ok());
  auto set_row_block = processor->row_block();
  EXPECT_EQ(1, set_row_block->row_count());
  YQLRow& set_row = set_row_block->row(0);

  // check row
  EXPECT_EQ(1, set_row.column(0).int32_value());
  EXPECT_EQ(3, set_row.column(1).int32_value());
  EXPECT_EQ("x", set_row.column(3).string_value());
  // check set
  EXPECT_EQ(YQLValue::InternalType::kSetValue, set_row.column(2).type());
  YQLSeqValuePB set_value = set_row.column(2).set_value();
  // check elems
  // returned set should have no duplicates
  EXPECT_EQ(4, set_value.elems_size());
  // set elements should be in default (ascending) order
  EXPECT_EQ(1, set_value.elems(0).int32_value());
  EXPECT_EQ(2, set_value.elems(1).int32_value());
  EXPECT_EQ(3, set_value.elems(2).int32_value());
  EXPECT_EQ(4, set_value.elems(3).int32_value());

  //------------------------------------------------------------------------------------------------
  // Testing List type
  //------------------------------------------------------------------------------------------------

  // Create table.
  const char *list_create_stmt =
      "CREATE TABLE list_test (id int PRIMARY KEY, v int, ls list<varchar>, c varchar);";
  s = processor->Run(list_create_stmt);
  CHECK(s.ok());

  // Insert Values
  std::string list_insert_stmt("INSERT INTO list_test (id, v, ls, c) values "
      "(1, 3, ['c', 'd', 'a', 'b', 'd', 'b'], 'x');");
  s = processor->Run(list_insert_stmt);
  CHECK_OK(s);

  // Check Select
  auto list_select_stmt = "SELECT * FROM list_test WHERE id = 1";
  s = processor->Run(list_select_stmt);
  CHECK(s.ok());
  auto list_row_block = processor->row_block();
  EXPECT_EQ(1, list_row_block->row_count());
  YQLRow& list_row = list_row_block->row(0);

  // check row
  EXPECT_EQ(1, list_row.column(0).int32_value());
  EXPECT_EQ(3, list_row.column(1).int32_value());
  EXPECT_EQ("x", list_row.column(3).string_value());
  // check set
  EXPECT_EQ(YQLValue::InternalType::kListValue, list_row.column(2).type());
  YQLSeqValuePB list_value = list_row.column(2).list_value();
  // check elems
  // lists should preserve input length (keep duplicates if any)
  EXPECT_EQ(6, list_value.elems_size());
  // list elements should preserve input order
  EXPECT_EQ("c", list_value.elems(0).string_value());
  EXPECT_EQ("d", list_value.elems(1).string_value());
  EXPECT_EQ("a", list_value.elems(2).string_value());
  EXPECT_EQ("b", list_value.elems(3).string_value());
  EXPECT_EQ("d", list_value.elems(4).string_value());
  EXPECT_EQ("b", list_value.elems(5).string_value());
}

TEST_F(YbSqlQuery, TestSystemLocal) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  YbSqlProcessor *processor = GetSqlProcessor();

  auto set_select_stmt = "SELECT * FROM system.local";
  CHECK_OK(processor->Run(set_select_stmt));

  // Validate rows.
  auto row_block = processor->row_block();
  EXPECT_EQ(1, row_block->row_count());
  YQLRow& row = row_block->row(0);
  EXPECT_EQ("127.0.0.1", row.column(2).inetaddress_value().ToString()); // broadcast address.
}

TEST_F(YbSqlQuery, TestSystemTablesWithRestart) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  YbSqlProcessor *processor = GetSqlProcessor();

  // Verify system table query works.
  ASSERT_OK(processor->Run("SELECT * FROM system.peers"));

  // Restart the cluster.
  ASSERT_OK(cluster_->RestartSync());

  // Verify system table query still works.
  ASSERT_OK(processor->Run("SELECT * FROM system.peers"));
}

TEST_F(YbSqlQuery, TestPagination) {
  // Init the simulated cluster.
  ASSERT_NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  YbSqlProcessor *processor = GetSqlProcessor();

  // Create test table.
  CHECK_OK(processor->Run("CREATE TABLE page_test (c int PRIMARY KEY);"));

  // Insert 10 different hash keys. They should go to different tablets.
  for (int i = 1; i <= 10; i++) {
    string stmt = Substitute("INSERT INTO page_test (c) VALUES ($0);", i);
    CHECK_OK(processor->Run(stmt));
  }

  // Do full-table query. All rows should be returned in one block.
  CHECK_VALID_STMT("SELECT * FROM page_test;");

  auto row_block = processor->row_block();
  EXPECT_EQ(10, row_block->row_count());
  int sum = 0;
  for (int i = 0; i < row_block->row_count(); i++) {
    sum += row_block->row(i).column(0).int32_value();
  }
  EXPECT_EQ(55, sum);
}

} // namespace sql
} // namespace yb
