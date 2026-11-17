"""Read the Bluetooth session-count reply apart from the data-export statuses."""
import ast
import asyncio
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
CLIENT = ROOT / "tools/ble/grinder-ble.py"


def client_methods(*names):
    """The named GrinderBLETool methods and the module's constants, executed alone.

    Importing the client would check the project venv and could install
    packages, so only these definitions are taken from its source.
    """
    tree = ast.parse(CLIENT.read_text())
    # What the method bodies and annotations refer to beyond the constants.
    namespace = {"asyncio": asyncio, "BleakGATTCharacteristic": object}
    constants = [node for node in tree.body if isinstance(node, ast.Assign)
                 and all(isinstance(target, ast.Name) and target.id.isupper()
                         for target in node.targets)]
    exec(compile(ast.Module(body=constants, type_ignores=[]), str(CLIENT), "exec"), namespace)
    tool = next(node for node in tree.body
                if isinstance(node, ast.ClassDef) and node.name == "GrinderBLETool")
    methods = [node for node in tool.body
               if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)) and node.name in names]
    assert {method.name for method in methods} == set(names)
    exec(compile(ast.Module(body=methods, type_ignores=[]), str(CLIENT), "exec"), namespace)
    return namespace


class FakeClient:
    def __init__(self, tool, reply):
        self.tool, self.reply = tool, reply

    async def write_gatt_char(self, _uuid, _data):
        # The grinder answers while the write is still in flight.
        await self.tool.on_data_status(None, bytearray(self.reply))


class Tool:
    pass


class SessionCountTest(unittest.TestCase):
    def setUp(self):
        self.namespace = client_methods("on_data_status", "get_session_count")
        for name in ("on_data_status", "get_session_count"):
            setattr(Tool, name, self.namespace[name])
        self.tool = Tool()
        self.tool.awaiting_session_count = False
        self.tool.session_count = 0
        self.tool.current_data_status = self.namespace["BLE_DATA_IDLE"]
        self.tool.receiving_data = True
        self.tool.printed = []
        self.tool.safe_print = self.tool.printed.append
        self.tool._update_status = self.tool.printed.append

    def ask_for_count(self, reply):
        self.tool.client = FakeClient(self.tool, reply)
        original_sleep = asyncio.sleep

        async def no_wait(_seconds):
            await original_sleep(0)

        asyncio.sleep = no_wait
        try:
            return asyncio.run(self.tool.get_session_count())
        finally:
            asyncio.sleep = original_sleep

    def test_counts_that_look_like_statuses_are_counts(self):
        # 33, 34 and 35 sessions arrive as the EXPORTING, COMPLETE and ERROR bytes.
        for count in (33, 34, 35, 289):
            self.assertEqual(self.ask_for_count([count & 0xFF, count >> 8]), count)
            self.assertTrue(self.tool.receiving_data)
            self.assertFalse(self.tool.awaiting_session_count)

    def test_statuses_outside_a_count_request_stay_statuses(self):
        complete = self.namespace["BLE_DATA_COMPLETE"]
        exporting = self.namespace["BLE_DATA_EXPORTING"]
        asyncio.run(self.tool.on_data_status(None, bytearray([exporting, 40])))
        self.assertEqual(self.tool.current_data_status, exporting)
        asyncio.run(self.tool.on_data_status(None, bytearray([complete, 0])))
        self.assertEqual(self.tool.current_data_status, complete)
        self.assertFalse(self.tool.receiving_data)
        self.assertEqual(self.tool.session_count, 0)


if __name__ == "__main__":
    unittest.main()
