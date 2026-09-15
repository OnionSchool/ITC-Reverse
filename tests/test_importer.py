import json
import tempfile
import unittest
from pathlib import Path

from itc_reimplementation.importer import import_legacy_configuration, write_configuration


class LegacyImportTests(unittest.TestCase):
    def test_imports_terminals_groups_and_members_without_writing_source(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            database = Path(directory) / "DB_Server.mdb"
            database.write_bytes(b"unchanged")

            def runner(command: list[str]) -> str:
                tables = {
                    "DB_Term": "ID,Name,Address,PlayVol,FwdAddr,TermType,Number,CCenter,TermVer,DefCallPri,DefLogonID\n2,教学楼,10.0.0.2,45,10.0.1.2,3,201,中心,2,600,operator\n",
                    "DB_Group": "GroupId,Name,Number\n7,一年级,G-1\n",
                    "DB_GroupMember": "GroupId,TermId\n7,2\n",
                }
                return tables[command[-1]]

            configuration = import_legacy_configuration(database, "/test/mdb-export", runner)
            output = Path(directory) / "legacy.json"
            write_configuration(configuration, output)

            self.assertEqual(database.read_bytes(), b"unchanged")
            self.assertEqual(configuration.terminals[0].address, "10.0.0.2")
            self.assertEqual(configuration.groups[0].terminal_ids, [2])
            self.assertEqual(json.loads(output.read_text(encoding="utf-8"))["groups"][0]["name"], "一年级")


if __name__ == "__main__":
    unittest.main()
