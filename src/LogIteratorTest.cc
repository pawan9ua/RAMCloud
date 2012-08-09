/* Copyright (c) 2011-2012 Stanford University
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR(S) DISCLAIM ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL AUTHORS BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "TestUtil.h"

#include "Log.h"
#include "LogIterator.h"
#include "ReplicaManager.h"
#include "Segment.h"
#include "SegmentManager.h"
#include "ServerList.h"

namespace RAMCloud {

class DoNothingHandlers : public Log::EntryHandlers {
  public:
    uint32_t getTimestamp(LogEntryType type, Buffer& buffer) { return 0; }
    bool checkLiveness(LogEntryType type, Buffer& buffer) { return true; }
    bool relocate(LogEntryType type, Buffer& oldBuffer,
                  HashTable::Reference newReference) { return true; }
};

/**
 * Unit tests for LogIterator.
 */
class LogIteratorTest : public ::testing::Test {
  public:
    Context context;
    ServerId serverId;
    ServerList serverList;
    ReplicaManager replicaManager;
    SegmentManager::Allocator allocator;
    SegmentManager segmentManager;
    DoNothingHandlers entryHandlers;
    Log l;
    char data[1000];

    LogIteratorTest()
        : context(),
          serverId(ServerId(57, 0)),
          serverList(context),
          replicaManager(context, serverId, 0),
          allocator(10 * 8192, 8192, 8192),
          segmentManager(context, serverId, allocator, replicaManager, 1.0),
          entryHandlers(),
          l(context, entryHandlers, segmentManager, replicaManager, true),
          data()
    {
    }

  private:
    DISALLOW_COPY_AND_ASSIGN(LogIteratorTest);
};

TEST_F(LogIteratorTest, constructor_emptyLog) {
    EXPECT_EQ(0, segmentManager.logIteratorCount);
    LogIterator i(l);
    EXPECT_EQ(&l, &i.log);
    EXPECT_EQ(0U, i.segmentList.size());
    EXPECT_FALSE(i.currentIterator);
    EXPECT_EQ(-1UL, i.currentSegmentId);
    EXPECT_TRUE(i.headLocked);
    EXPECT_EQ(1, segmentManager.logIteratorCount);
}

TEST_F(LogIteratorTest, constructor_singleSegmentLog) {
    l.sync();
    l.append(LOG_ENTRY_TYPE_OBJ, data, sizeof(data), true);

    EXPECT_EQ(0, segmentManager.logIteratorCount);
    LogIterator i(l);
    EXPECT_EQ(&l, &i.log);
    EXPECT_EQ(0U, i.segmentList.size());
    EXPECT_TRUE(i.currentIterator);
    EXPECT_EQ(0U, i.currentSegmentId);
    EXPECT_TRUE(i.headLocked);
    EXPECT_EQ(1, segmentManager.logIteratorCount);
}

TEST_F(LogIteratorTest, constructor_multiSegmentLog) {
    l.sync();
    while (l.getHeadPosition().getSegmentId() == 0)
        l.append(LOG_ENTRY_TYPE_OBJ, data, sizeof(data), true);

    EXPECT_EQ(0, segmentManager.logIteratorCount);
    LogIterator i(l);
    EXPECT_EQ(&l, &i.log);
    EXPECT_EQ(1U, i.segmentList.size());
    EXPECT_TRUE(i.currentIterator);
    EXPECT_EQ(0U, i.currentSegmentId);
    EXPECT_FALSE(i.headLocked);
    EXPECT_EQ(1, segmentManager.logIteratorCount);
}

TEST_F(LogIteratorTest, destructor) {
    // ensure the append lock is taken and released on destruction
    {
        LogIterator i(l);
        EXPECT_TRUE(i.headLocked);
        EXPECT_NE(0, l.appendLock.mutex.load());
    }
    EXPECT_EQ(0, l.appendLock.mutex.load());
    EXPECT_EQ(0, segmentManager.logIteratorCount);
}

TEST_F(LogIteratorTest, isDone_simple) {
    {
        LogIterator i(l);
        EXPECT_TRUE(i.isDone());
    }

    l.sync();

    {
        LogIterator i(l);
        EXPECT_FALSE(i.isDone());
        EXPECT_EQ(LOG_ENTRY_TYPE_SEGHEADER, i.getType());
        i.next();

        EXPECT_FALSE(i.isDone());
        EXPECT_EQ(LOG_ENTRY_TYPE_LOGDIGEST, i.getType());
        i.next();

        EXPECT_TRUE(i.isDone());
    }

    l.append(LOG_ENTRY_TYPE_OBJ, data, sizeof(data), true);
    LogIterator i(l);
    EXPECT_FALSE(i.isDone());

    int count;
    for (count = 0; !i.isDone(); count++)
        i.next();
    EXPECT_EQ(3, count);
}

#if 0
TEST_F(LogIteratorTest, isDone_multiSegment) {
    int origObjCnt = 0;

    while (l.getHeadPosition().getSegmentId() == 0) {
        l.append(LOG_ENTRY_TYPE_OBJ, data, sizeof(data), true);
        origObjCnt++;
    }
    l.append(LOG_ENTRY_TYPE_OBJTOMB, data, sizeof(data), true);

    LogEntryType lastType = LOG_ENTRY_TYPE_INVALID;
    int objCnt = 0, tombCnt = 0, otherCnt = 0;
    for (LogIterator i(l); !i.isDone(); i.next()) {
        lastType = i.getType();
        if (lastType == LOG_ENTRY_TYPE_OBJ)
            objCnt++;
        else if (lastType == LOG_ENTRY_TYPE_OBJTOMB)
            tombCnt++;
        else
            otherCnt++;
    }
    EXPECT_EQ(origObjCnt, objCnt);
    EXPECT_EQ(1, tombCnt);
    EXPECT_EQ(4, otherCnt);
    EXPECT_EQ(LOG_ENTRY_TYPE_OBJTOMB, lastType);
}
#endif

TEST_F(LogIteratorTest, next) {
    {
        LogIterator i(l);
        EXPECT_TRUE(i.headLocked);
        EXPECT_FALSE(i.currentIterator);
        EXPECT_EQ(0U, i.segmentList.size());
        EXPECT_EQ(-1UL, i.currentSegmentId);
    }

    l.sync();
    l.append(LOG_ENTRY_TYPE_OBJ, data, sizeof(data), true);

    {
        LogIterator i(l);
        EXPECT_TRUE(i.headLocked);
        EXPECT_TRUE(i.currentIterator);
        EXPECT_EQ(0U, i.currentSegmentId);
        EXPECT_EQ(0U, i.segmentList.size());
        EXPECT_FALSE(i.currentIterator->isDone());

        // We have <SegHeader, LogDigest, Object> in the log.
        Segment* lastSegment = i.currentIterator->segment;
        EXPECT_EQ(LOG_ENTRY_TYPE_SEGHEADER, i.getType());

        i.next();
        EXPECT_EQ(LOG_ENTRY_TYPE_LOGDIGEST, i.getType());
        EXPECT_EQ(lastSegment, i.currentIterator->segment);

        i.next();
        EXPECT_EQ(LOG_ENTRY_TYPE_OBJ, i.getType());
        EXPECT_EQ(lastSegment, i.currentIterator->segment);

        i.next();
        EXPECT_EQ(lastSegment, i.currentIterator->segment);
        EXPECT_TRUE(i.isDone());
    }

    while (l.getHeadPosition().getSegmentId() == 0)
        l.append(LOG_ENTRY_TYPE_OBJ, data, sizeof(data), true);

    {
        LogIterator i(l);
        EXPECT_FALSE(i.headLocked);
        EXPECT_TRUE(i.currentIterator);
        EXPECT_EQ(0U, i.currentSegmentId);
        EXPECT_EQ(1U, i.segmentList.size());

        while (!i.headLocked)
            i.next();

        EXPECT_TRUE(i.headLocked);
        EXPECT_TRUE(i.currentIterator);
        EXPECT_EQ(1U, i.currentSegmentId);
        EXPECT_EQ(0U, i.segmentList.size());

        while (!i.isDone())
            i.next();

        i.next();
        EXPECT_TRUE(i.isDone());
        EXPECT_TRUE(i.headLocked);
        EXPECT_FALSE(i.currentIterator);
        EXPECT_EQ(1U, i.currentSegmentId);
        EXPECT_EQ(0U, i.segmentList.size());

        // Ensure extra next()s are legal.
        for (int j = 0; j < 50; j++) {
            i.next();
            EXPECT_TRUE(i.isDone());
        }
    }

    {
        // Inject a "cleaner" segment into the log
        segmentManager.setSurvivorSegmentReserve(1);
        LogSegment* cleanerSeg = segmentManager.allocSurvivor(5);
        EXPECT_EQ(2U, cleanerSeg->id);
        segmentManager.changeState(*cleanerSeg,
                                   SegmentManager::NEWLY_CLEANABLE);

        uint64_t lastSegmentId = -1;
        LogIterator i(l);
        while (!i.isDone()) {
            lastSegmentId = i.currentSegmentId;
            i.next();
        }
        EXPECT_EQ(2U, lastSegmentId);

        segmentManager.free(cleanerSeg);
    }
}

TEST_F(LogIteratorTest, populateSegmentList) {
        l.sync();
        LogSegment* seg1 = segmentManager.allocHead();
        LogSegment* seg2 = segmentManager.allocHead();
        LogSegment* seg3 = segmentManager.allocHead();

        LogIterator i(l);
        EXPECT_EQ(3U, i.segmentList.size());

        i.segmentList.clear();
        i.populateSegmentList(0);
        EXPECT_EQ(4U, i.segmentList.size());

        i.segmentList.clear();
        i.populateSegmentList(1);
        EXPECT_EQ(3U, i.segmentList.size());

        i.segmentList.clear();
        i.populateSegmentList(2);
        EXPECT_EQ(2U, i.segmentList.size());

        i.segmentList.clear();
        i.populateSegmentList(3);
        EXPECT_EQ(1U, i.segmentList.size());

        i.segmentList.clear();
        i.populateSegmentList(4);
        EXPECT_EQ(0U, i.segmentList.size());

        // ensure segments are sorted
        i.segmentList.clear();
        *const_cast<uint64_t*>(&seg1->id) = 15;
        *const_cast<uint64_t*>(&seg2->id) = 10;
        *const_cast<uint64_t*>(&seg3->id) = 12;
        i.populateSegmentList(1);
        EXPECT_EQ(3U, i.segmentList.size());
        EXPECT_EQ(seg2, i.segmentList[2]);
        EXPECT_EQ(seg3, i.segmentList[1]);
        EXPECT_EQ(seg1, i.segmentList[0]);
}

// Ensure that the "cleaner", doesn't change the contents of the log
// until after iteration has completed (and iterators have been
// destroyed).
//
// This relies on some tricky SegmentManager interactions. Perhaps the
// test belongs there more.
#if 0
TEST_F(LogIteratorTest, cleanerInteraction) {
    while (l.getHeadPosition().getSegmentId() == 0)
        l.append(LOG_ENTRY_TYPE_OBJ, &serverId, sizeof(serverId), true);

    Tub<LogIterator> i;
    i.construct(l);
    SegmentVector clean;
    std::vector<void*> unused;

    Segment cleanerSeg(*serverId, 2, alignedBuf, sizeof(alignedBuf));
    cleanerSeg.close(NULL);

    l.getNewCleanableSegments(clean);
    l.cleaningInto(&cleanerSeg);

    // Fake having cleaned seg 0
    EXPECT_EQ(1U, clean.size());
    EXPECT_EQ(1U, l.cleanableList.size());
    clean.push_back(&l.cleanableList.back());
    l.cleaningComplete(clean, unused);

    // Sanity: seg 2 (cleaner seg) mustn't immediately become part of the log
    while (!i->isDone()) {
        foreach (Segment* s, i->segmentList)
            EXPECT_NE(2U, s->getId());
        i->next();
    }

    i.destroy();
    i.construct(l);

    // Nor must seg 2 join the log when a new head appears.
    while (l.getHeadPosition().getSegmentId() == 1)
        l.append(LOG_ENTRY_TYPE_OBJ, &serverId, sizeof(serverId), true);
    l.append(LOG_ENTRY_TYPE_OBJ, &serverId, sizeof(serverId), true);
    while (!i->isDone()) {
        foreach (Segment* s, i->segmentList)
            EXPECT_NE(2U, s->getId());
        i->next();
    }

    EXPECT_EQ(1U, l.cleanablePendingDigestList.size());
    l.cleanablePendingDigestList.pop_back();
}
#endif
} // namespace RAMCloud