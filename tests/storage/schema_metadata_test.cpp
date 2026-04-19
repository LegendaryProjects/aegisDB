#include <filesystem>
#include <string>

#include <gtest/gtest.h>

#include "storage/storage_engine.h"

using namespace aegis;

TEST(StorageEngineTest, SchemaMetadataRoundTrip) {
    const std::string db_file = "schema_metadata_test.db";
    const std::string log_file = "schema_metadata_test.log";

    if (std::filesystem::exists(db_file)) {
        std::filesystem::remove(db_file);
    }
    if (std::filesystem::exists(log_file)) {
        std::filesystem::remove(log_file);
    }

    StorageEngine engine(db_file, log_file);
    engine.Boot();

    Command cmd;
    cmd.type = Command::Type::INSERT;
    cmd.key = "SCHEMA_users";
    cmd.value = "id*,name";
    ASSERT_TRUE(engine.Apply(cmd));

    std::string schema_after_schema_insert;
    cmd.type = Command::Type::GET;
    cmd.key = "SCHEMA_users";
    ASSERT_TRUE(engine.Apply(cmd, &schema_after_schema_insert));
    EXPECT_EQ(schema_after_schema_insert, "id*,name");

    cmd.type = Command::Type::INSERT;
    cmd.key = "INDEX_users";
    cmd.value = "";
    ASSERT_TRUE(engine.Apply(cmd));

    std::string schema;
    cmd.type = Command::Type::GET;
    cmd.key = "SCHEMA_users";
    ASSERT_TRUE(engine.Apply(cmd, &schema));
    EXPECT_EQ(schema, "id*,name");

    std::string index;
    cmd.type = Command::Type::GET;
    cmd.key = "INDEX_users";
    ASSERT_TRUE(engine.Apply(cmd, &index));
    EXPECT_EQ(index, "");

    engine.Shutdown();

    if (std::filesystem::exists(db_file)) {
        std::filesystem::remove(db_file);
    }
    if (std::filesystem::exists(log_file)) {
        std::filesystem::remove(log_file);
    }
}
