import unittest

from itc_reimplementation.protocol import BroadcastController, ProtocolError


class BroadcastControllerTests(unittest.TestCase):
    def setUp(self) -> None:
        self.controller = BroadcastController("admin", "admin")

    def execute(self, command: str, authenticated: bool = True) -> str:
        return self.controller.execute(command, authenticated)[0]

    def test_requires_login_and_accepts_known_credentials(self) -> None:
        with self.assertRaisesRegex(ProtocolError, "not logon"):
            self.controller.execute("session new A 400 10 1", False)
        self.assertEqual(self.execute("logon 0 admin admin", False), "000 ok")
        with self.assertRaisesRegex(ProtocolError, "invalid password"):
            self.execute("logon 0 admin no")

    def test_session_lifecycle(self) -> None:
        self.assertEqual(self.execute("session new Morning 400 10 1"), "000 1")
        self.assertEqual(self.execute("session add_term 1 ,1001"), "000 ok")
        self.assertEqual(self.execute("session set 1 STAT=1 PLAY_TIME=12"), "000 ok")
        self.assertEqual(self.execute("session get 1 STAT"), "000 STAT=1")
        self.assertEqual(self.execute("session get 1 PLAY_TIME"), "000 PLAY_TIME=12")
        self.assertEqual(self.execute("session rm_term 1 ,1001"), "000 ok")
        self.assertEqual(self.execute("session rm 1"), "000 ok")

    def test_invalid_session_is_reported(self) -> None:
        with self.assertRaisesRegex(ProtocolError, "invalid session"):
            self.execute("session get 99 STAT")


if __name__ == "__main__":
    unittest.main()
