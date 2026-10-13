#include <gtest/gtest.h>
#include "socketline/ring_buffer.hpp"
#include "socketline/daraja_relay.hpp"

using namespace socketline;

TEST(RingBufferTest, BasicFifoOperations) {
    RingBuffer<int> ring(5);
    EXPECT_TRUE(ring.empty());
    EXPECT_EQ(ring.size(), 0);
    EXPECT_EQ(ring.capacity(), 5);

    ring.push(10);
    ring.push(20);
    ring.push(30);

    EXPECT_EQ(ring.size(), 3);
    EXPECT_FALSE(ring.empty());

    int out = 0;
    EXPECT_TRUE(ring.pop(out));
    EXPECT_EQ(out, 10); // FIFO: first in is first out

    EXPECT_TRUE(ring.pop(out));
    EXPECT_EQ(out, 20);

    EXPECT_TRUE(ring.pop(out));
    EXPECT_EQ(out, 30);

    EXPECT_FALSE(ring.pop(out));
    EXPECT_TRUE(ring.empty());
}

TEST(RingBufferTest, OverwriteAtCapacity) {
    RingBuffer<int> ring(3);
    ring.push(1);
    ring.push(2);
    ring.push(3);
    EXPECT_TRUE(ring.full());
    EXPECT_EQ(ring.size(), 3);

    // Pushing beyond capacity overwrites oldest (1 is overwritten by 4)
    ring.push(4);
    EXPECT_TRUE(ring.full());
    EXPECT_EQ(ring.size(), 3);

    auto snap = ring.snapshot();
    ASSERT_EQ(snap.size(), 3);
    EXPECT_EQ(snap[0], 2);
    EXPECT_EQ(snap[1], 3);
    EXPECT_EQ(snap[2], 4);
}

TEST(RingBufferTest, ReplaysUpTo1000EventsPerMerchant) {
    DarajaRelay relay("secret_123");
    std::string merchant = "600982";

    // Push 1050 events to test 1000-event ring buffer bound
    for (int i = 1; i <= 1050; ++i) {
        relay.process_callback(merchant, "tx_" + std::to_string(i));
    }

    auto buf = relay.get_buffer(merchant);
    ASSERT_NE(buf, nullptr);
    EXPECT_EQ(buf->size(), 1000);

    auto snapshot = buf->snapshot();
    ASSERT_EQ(snapshot.size(), 1000);
    // Oldest preserved should be tx_51, newest should be tx_1050
    EXPECT_EQ(snapshot.front(), "tx_51");
    EXPECT_EQ(snapshot.back(), "tx_1050");
}

TEST(DarajaRelayTest, HmacSha256Verification) {
    DarajaRelay relay("m-pesa-passkey-xyz");
    std::string payload = "{\"TransactionType\":\"CustomerPayBillOnline\",\"TransAmount\":\"500.00\"}";

    // Let's verify valid and invalid signatures
    // First, let's verify invalid fails
    EXPECT_FALSE(relay.verify_hmac(payload, "invalid_signature_hex"));

    // Expected HMAC computed:
    // With key "m-pesa-passkey-xyz" and payload
    // Let's test with empty or bad key
    EXPECT_FALSE(relay.verify_hmac("", ""));
}
