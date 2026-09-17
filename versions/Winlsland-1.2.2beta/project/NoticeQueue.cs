using System.Collections.Generic;

namespace WinIsland
{
    // Pending notifications remain in memory only; updates replace the matching event in place.
    internal sealed class NoticeQueue
    {
        private readonly LinkedList<Notice> items = new LinkedList<Notice>();
        private readonly Dictionary<string, LinkedListNode<Notice>> byKey = new Dictionary<string, LinkedListNode<Notice>>();
        internal int Count { get { return items.Count; } }
        internal void Enqueue(Notice notice)
        {
            LinkedListNode<Notice> existing;
            if (byKey.TryGetValue(notice.Key, out existing)) existing.Value = notice;
            else byKey.Add(notice.Key, items.AddLast(notice));
        }
        internal Notice Dequeue()
        {
            Notice notice = items.First.Value;
            items.RemoveFirst(); byKey.Remove(notice.Key);
            return notice;
        }
        internal void Clear() { items.Clear(); byKey.Clear(); }
    }
}
