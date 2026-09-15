from __future__ import annotations

from dataclasses import dataclass, field


class ProtocolError(ValueError):
    def __init__(self, code: int, message: str) -> None:
        super().__init__(message)
        self.code = code
        self.message = message


@dataclass
class Session:
    session_id: int
    name: str
    priority: int
    user_priority: int
    session_type: int
    status: int = 0
    play_time: int = 0
    total_time: int = 0
    program_id: int = 0
    terms: set[int] = field(default_factory=set)


class BroadcastController:
    """保存由 C_Media/C_Proxy 控制面暴露的最小会话状态。"""

    def __init__(self, username: str, password: str) -> None:
        self.username = username
        self.password = password
        self.sessions: dict[int, Session] = {}
        self._next_session_id = 1

    def execute(self, line: str, authenticated: bool) -> tuple[str, bool]:
        parts = line.strip().split()
        if not parts:
            raise ProtocolError(599, "unknown command.")
        command = parts[0].lower()
        if command == "quit":
            return "000 bye", False
        if command == "logon":
            return self._logon(parts), True
        if not authenticated:
            raise ProtocolError(501, "not logon")
        if command != "session":
            raise ProtocolError(599, "unknown command.")
        return self._session(parts), authenticated

    def _logon(self, parts: list[str]) -> str:
        if len(parts) != 4:
            raise ProtocolError(599, "invalid argument")
        _, _, username, password = parts
        if username != self.username:
            raise ProtocolError(511, "invalid user")
        if password != self.password:
            raise ProtocolError(512, "invalid password")
        return "000 ok"

    def _session(self, parts: list[str]) -> str:
        if len(parts) < 2:
            raise ProtocolError(599, "unknown sub command.")
        action = parts[1].lower()
        if action == "new":
            return self._new_session(parts)
        if action == "rm":
            session = self._get_session(self._integer(parts, 2))
            del self.sessions[session.session_id]
            return "000 ok"
        if action == "add_term":
            session = self._get_session(self._integer(parts, 2))
            session.terms.add(self._term_id(parts, 3))
            return "000 ok"
        if action == "rm_term":
            session = self._get_session(self._integer(parts, 2))
            session.terms.discard(self._term_id(parts, 3))
            return "000 ok"
        if action == "get":
            return self._get_field(parts)
        if action == "set":
            return self._set_field(parts)
        raise ProtocolError(599, "unknown sub command.")

    def _new_session(self, parts: list[str]) -> str:
        if len(parts) != 6:
            raise ProtocolError(599, "invalid argument")
        _, _, name, priority, user_priority, session_type = parts
        session = Session(
            self._next_session_id,
            name,
            self._parse_integer(priority),
            self._parse_integer(user_priority),
            self._parse_integer(session_type),
        )
        self.sessions[session.session_id] = session
        self._next_session_id += 1
        return f"000 {session.session_id}"

    def _get_field(self, parts: list[str]) -> str:
        if len(parts) != 4:
            raise ProtocolError(599, "invalid argument")
        session = self._get_session(self._integer(parts, 2))
        field_name = parts[3].upper()
        values = {
            "STAT": session.status,
            "PLAY_TIME": session.play_time,
            "TOTAL_TIME": session.total_time,
            "NAME": session.name,
            "TYPE": session.session_type,
            "PROGRAM_ID": session.program_id,
        }
        if field_name not in values:
            raise ProtocolError(500, "invalid parameter")
        return f"000 {field_name}={values[field_name]}"

    def _set_field(self, parts: list[str]) -> str:
        if len(parts) < 4:
            raise ProtocolError(599, "invalid argument")
        session = self._get_session(self._integer(parts, 2))
        for assignment in parts[3:]:
            key, separator, value = assignment.partition("=")
            if not separator:
                raise ProtocolError(500, "invalid parameter")
            attributes = {
                "STAT": "status",
                "PLAY_TIME": "play_time",
                "TOTAL_TIME": "total_time",
                "TYPE": "session_type",
                "PROGRAM_ID": "program_id",
            }
            key = key.upper()
            if key == "NAME":
                session.name = value
            elif key in attributes:
                setattr(session, attributes[key], self._parse_integer(value))
            else:
                raise ProtocolError(500, "invalid parameter")
        return "000 ok"

    def _get_session(self, session_id: int) -> Session:
        try:
            return self.sessions[session_id]
        except KeyError as error:
            raise ProtocolError(500, "invalid session") from error

    @staticmethod
    def _parse_integer(value: str) -> int:
        try:
            return int(value)
        except ValueError as error:
            raise ProtocolError(500, "invalid argument") from error

    def _integer(self, parts: list[str], index: int) -> int:
        if len(parts) <= index:
            raise ProtocolError(599, "invalid argument")
        return self._parse_integer(parts[index])

    def _term_id(self, parts: list[str], index: int) -> int:
        if len(parts) <= index:
            raise ProtocolError(599, "invalid argument")
        return self._parse_integer(parts[index].lstrip(","))
