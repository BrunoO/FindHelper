#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include "usn/UsnJournalQueue.h"

// ---------------------------------------------------------------------------
// Basic correctness
// ---------------------------------------------------------------------------

TEST_CASE("UsnJournalQueue - Push and Pop round-trip") {
    UsnJournalQueue q(4);
    const std::vector<char> payload = {'a', 'b', 'c'};
    CHECK(q.Push(std::vector<char>(payload)));

    std::vector<char> out;
    CHECK(q.Pop(out));
    CHECK(out == payload);
}

TEST_CASE("UsnJournalQueue - Size tracking") {
    UsnJournalQueue q(4);
    CHECK(q.Size() == 0);

    q.Push({'x'});
    CHECK(q.Size() == 1);

    q.Push({'y'});
    CHECK(q.Size() == 2);

    std::vector<char> out;
    q.Pop(out);
    CHECK(q.Size() == 1);

    q.Pop(out);
    CHECK(q.Size() == 0);
}

TEST_CASE("UsnJournalQueue - FIFO ordering") {
    UsnJournalQueue q(4);
    q.Push({'1'});
    q.Push({'2'});
    q.Push({'3'});

    std::vector<char> out;
    q.Pop(out);
    CHECK(out == std::vector<char>{'1'});
    q.Pop(out);
    CHECK(out == std::vector<char>{'2'});
    q.Pop(out);
    CHECK(out == std::vector<char>{'3'});
}

// ---------------------------------------------------------------------------
// Stop / shutdown behaviour
// ---------------------------------------------------------------------------

TEST_CASE("UsnJournalQueue - IsStopped reflects Stop()") {
    UsnJournalQueue q(4);
    CHECK_FALSE(q.IsStopped());
    q.Stop();
    CHECK(q.IsStopped());
}

TEST_CASE("UsnJournalQueue - Push returns false after Stop") {
    UsnJournalQueue q(4);
    q.Stop();
    const bool result = q.Push({'x'});
    CHECK_FALSE(result);
    CHECK(q.Size() == 0);
}

TEST_CASE("UsnJournalQueue - Pop returns false on stopped empty queue") {
    UsnJournalQueue q(4);
    q.Stop();
    std::vector<char> out;
    const bool result = q.Pop(out);
    CHECK_FALSE(result);
}

TEST_CASE("UsnJournalQueue - Stop drains: remaining items still pop after Stop") {
    UsnJournalQueue q(4);
    q.Push({'a'});
    q.Push({'b'});
    q.Stop();

    // Items pushed before Stop() must still be retrievable.
    std::vector<char> out1;
    std::vector<char> out2;
    CHECK(q.Pop(out1));
    CHECK(q.Pop(out2));

    // Empty + stopped → returns false.
    std::vector<char> out3;
    CHECK_FALSE(q.Pop(out3));
}

// ---------------------------------------------------------------------------
// Concurrency: saturation and unblock
// ---------------------------------------------------------------------------

TEST_CASE("UsnJournalQueue - saturation: producer blocks until consumer pops") {
    // Fill the queue to max capacity, then verify a Push from another thread
    // blocks until we Pop one item to free space.
    UsnJournalQueue q(2);
    q.Push({'a'});
    q.Push({'b'});  // queue now full

    std::atomic push_done{false};
    std::thread producer([&q, &push_done] {
        q.Push({'c'});  // must block until space is freed
        push_done.store(true);
    });

    // Give the producer thread time to enter the blocking wait inside Push().
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    CHECK_FALSE(push_done.load());  // still blocked

    // Freeing one slot must unblock the producer.
    std::vector<char> out;
    q.Pop(out);

    producer.join();
    CHECK(push_done.load());
    CHECK(q.Size() == 2);  // 'b' and 'c'
}

TEST_CASE("UsnJournalQueue - Pop blocks on empty queue, unblocks on Push") {
    UsnJournalQueue q(4);

    std::atomic pop_done{false};
    std::vector<char> received;
    std::thread consumer([&q, &pop_done, &received] {
        std::vector<char> out;
        q.Pop(out);  // must block until a buffer is pushed
        received = out;
        pop_done.store(true);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK_FALSE(pop_done.load());  // still blocked

    q.Push({'z'});
    consumer.join();

    CHECK(pop_done.load());
    CHECK(received == std::vector<char>{'z'});
}

TEST_CASE("UsnJournalQueue - shutdown unblocks blocked Push") {
    UsnJournalQueue q(1);
    q.Push({'a'});  // fill the queue

    std::atomic push_returned_false{false};
    std::thread producer([&q, &push_returned_false] {
        const bool ok = q.Push({'b'});  // blocks until Stop()
        push_returned_false.store(!ok);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    q.Stop();
    producer.join();

    CHECK(push_returned_false.load());
}

TEST_CASE("UsnJournalQueue - shutdown unblocks blocked Pop") {
    UsnJournalQueue q(4);  // empty — Pop will block

    std::atomic pop_returned_false{false};
    std::thread consumer([&q, &pop_returned_false] {
        std::vector<char> out;
        const bool ok = q.Pop(out);  // blocks until Stop()
        pop_returned_false.store(!ok);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    q.Stop();
    consumer.join();

    CHECK(pop_returned_false.load());
}
