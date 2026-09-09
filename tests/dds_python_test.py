import threading

import _dds_abstract as dds


def main():
    assert dds.__version__ == "1.0.0"
    server = dds.Server("inproc://python-events", "inproc://python-rpc")
    client1 = dds.Client("inproc://python-events", "inproc://python-rpc")
    client2 = dds.Client("inproc://python-events", "inproc://python-rpc")
    event1 = threading.Event()
    event2 = threading.Event()
    server.on_request(lambda value: b"python:" + value)
    client1.on_broadcast(lambda value: event1.set())
    client2.on_broadcast(lambda value: event2.set())
    server.start()
    client1.start()
    client2.start()
    assert client1.request(b"one") == b"python:one"
    assert client2.request(b"two") == b"python:two"
    server.publish(b"event")
    assert event1.wait(1.0)
    assert event2.wait(1.0)
    client2.stop()
    client1.stop()
    server.stop()


if __name__ == "__main__":
    main()
