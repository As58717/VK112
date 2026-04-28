#!/usr/bin/env python3
"""
面向“快速查看 100 万网页”的一体化示例：
- 高速抓取（异步）
- HTML 清洗/正文抽取/去重
- 本地检索（SQLite FTS5 + 向量近邻 + 链接图）
- Top-K 召回后交给 Agent（这里用占位逻辑模拟核查/摘要）

说明：这是可运行的工程骨架，重点在架构与操作流程。
生产环境建议拆分为多进程/多机任务队列，并替换为专用组件（Kafka、ClickHouse、OpenSearch、Milvus 等）。
"""

from __future__ import annotations

import argparse
import asyncio
import hashlib
import json
import math
import re
import sqlite3
import time
from collections import Counter
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Sequence, Set, Tuple
from urllib.parse import urljoin, urlparse
from urllib.request import Request, urlopen

# 可选依赖：若缺失会自动退化
try:
    import aiohttp
except Exception:
    aiohttp = None

try:
    from bs4 import BeautifulSoup
except Exception:
    BeautifulSoup = None

try:
    import trafilatura
except Exception:
    trafilatura = None

try:
    import numpy as np
except Exception:
    np = None


USER_AGENT = "MillionWebPipeline/1.0 (+local-demo)"
STOPWORDS = {
    "the", "a", "an", "is", "are", "of", "to", "and", "or", "for", "on", "in", "with", "that", "this", "it",
    "了", "的", "是", "在", "和", "及", "与", "或", "对", "为", "并", "一个", "我们", "你", "我",
}
TOKEN_RE = re.compile(r"[A-Za-z0-9_\-\u4e00-\u9fff]+")


@dataclass
class Page:
    url: str
    title: str
    html: str
    text: str
    out_links: List[str]
    fetched_at: float


def ensure_schema(db_path: str) -> None:
    conn = sqlite3.connect(db_path)
    conn.execute("PRAGMA journal_mode=WAL;")
    conn.execute("PRAGMA synchronous=NORMAL;")

    conn.execute(
        """
        CREATE TABLE IF NOT EXISTS pages(
            id INTEGER PRIMARY KEY,
            url TEXT UNIQUE,
            url_hash TEXT UNIQUE,
            title TEXT,
            text TEXT,
            content_hash TEXT,
            fetched_at REAL
        )
        """
    )

    conn.execute(
        """
        CREATE VIRTUAL TABLE IF NOT EXISTS pages_fts
        USING fts5(url, title, text, content='pages', content_rowid='id')
        """
    )

    conn.execute(
        """
        CREATE TABLE IF NOT EXISTS embeddings(
            page_id INTEGER PRIMARY KEY,
            vector BLOB,
            dim INTEGER,
            FOREIGN KEY(page_id) REFERENCES pages(id)
        )
        """
    )

    conn.execute(
        """
        CREATE TABLE IF NOT EXISTS links(
            src_id INTEGER,
            dst_url TEXT,
            FOREIGN KEY(src_id) REFERENCES pages(id)
        )
        """
    )

    conn.execute(
        """
        CREATE TRIGGER IF NOT EXISTS pages_ai AFTER INSERT ON pages BEGIN
          INSERT INTO pages_fts(rowid, url, title, text)
          VALUES (new.id, new.url, new.title, new.text);
        END;
        """
    )
    conn.execute(
        """
        CREATE TRIGGER IF NOT EXISTS pages_ad AFTER DELETE ON pages BEGIN
          INSERT INTO pages_fts(pages_fts, rowid, url, title, text)
          VALUES('delete', old.id, old.url, old.title, old.text);
        END;
        """
    )
    conn.execute(
        """
        CREATE TRIGGER IF NOT EXISTS pages_au AFTER UPDATE ON pages BEGIN
          INSERT INTO pages_fts(pages_fts, rowid, url, title, text)
          VALUES('delete', old.id, old.url, old.title, old.text);
          INSERT INTO pages_fts(rowid, url, title, text)
          VALUES(new.id, new.url, new.title, new.text);
        END;
        """
    )

    conn.commit()
    conn.close()


def normalize_url(url: str) -> str:
    parsed = urlparse(url)
    scheme = parsed.scheme.lower() or "http"
    netloc = parsed.netloc.lower()
    path = parsed.path or "/"
    return f"{scheme}://{netloc}{path}" + (f"?{parsed.query}" if parsed.query else "")


def hash_text(text: str) -> str:
    return hashlib.sha1(text.encode("utf-8", errors="ignore")).hexdigest()


def tokenize(text: str) -> List[str]:
    return [t.lower() for t in TOKEN_RE.findall(text) if t.lower() not in STOPWORDS and len(t) > 1]


def _l2_norm(values: List[float]) -> float:
    return math.sqrt(sum(v * v for v in values))


def _dot(a: Sequence[float], b: Sequence[float]) -> float:
    return sum(x * y for x, y in zip(a, b))


def text_to_vector(text: str, dim: int = 512):
    """轻量哈希向量（无需模型），用于演示本地向量检索。"""
    if np is not None:
        vec = np.zeros(dim, dtype=np.float32)
    else:
        vec = [0.0] * dim
    tokens = tokenize(text)
    if not tokens:
        return vec
    counts = Counter(tokens)
    for tok, tf in counts.items():
        h = int(hashlib.md5(tok.encode("utf-8")).hexdigest(), 16)
        idx = h % dim
        sign = 1.0 if (h >> 1) & 1 else -1.0
        vec[idx] += sign * (1.0 + math.log1p(tf))
    norm = float(np.linalg.norm(vec)) if np is not None else _l2_norm(vec)
    if norm > 0:
        if np is not None:
            vec /= norm
        else:
            vec = [v / norm for v in vec]
    return vec


def blob_from_vec(vec) -> bytes:
    if np is not None:
        return vec.astype(np.float32).tobytes()
    return json.dumps([float(v) for v in vec], ensure_ascii=False).encode("utf-8")


def vec_from_blob(blob: bytes, dim: int):
    if np is not None:
        return np.frombuffer(blob, dtype=np.float32, count=dim)
    values = json.loads(blob.decode("utf-8"))
    return values[:dim]


def _fallback_extract_text_and_links(url: str, html: str) -> Tuple[str, str, List[str]]:
    title_match = re.search(r"<title[^>]*>(.*?)</title>", html, re.IGNORECASE | re.DOTALL)
    title = re.sub(r"\s+", " ", title_match.group(1)).strip() if title_match else ""

    links: List[str] = []
    for m in re.finditer(r"""<a[^>]+href=["']?([^"' >]+)""", html, re.IGNORECASE):
        link = urljoin(url, m.group(1).strip())
        parsed = urlparse(link)
        if parsed.scheme in {"http", "https", "file"}:
            links.append(link)

    cleaned = re.sub(r"(?is)<script.*?>.*?</script>", " ", html)
    cleaned = re.sub(r"(?is)<style.*?>.*?</style>", " ", cleaned)
    cleaned = re.sub(r"(?s)<[^>]+>", " ", cleaned)
    text = re.sub(r"\s+", " ", cleaned).strip()
    return title, text, links


def extract_text_and_links(url: str, html: str) -> Tuple[str, str, List[str]]:
    title = ""
    text = ""
    links: List[str] = []

    if trafilatura is not None:
        extracted = trafilatura.extract(html, include_comments=False, include_tables=False)
        if extracted:
            text = extracted

    if BeautifulSoup is None:
        title, text2, links = _fallback_extract_text_and_links(url, html)
        if not text:
            text = text2
    else:
        soup = BeautifulSoup(html, "html.parser")
        if soup.title and soup.title.string:
            title = soup.title.string.strip()

        if not text:
            for tag in soup(["script", "style", "noscript"]):
                tag.extract()
            text = re.sub(r"\s+", " ", soup.get_text(" ")).strip()

        for a in soup.find_all("a", href=True):
            link = urljoin(url, a["href"]).strip()
            parsed = urlparse(link)
            if parsed.scheme in {"http", "https"} and parsed.netloc:
                links.append(normalize_url(link))

    # 限制长度，防止超长页面撑爆本地存储
    if len(text) > 200_000:
        text = text[:200_000]
    if len(title) > 2_000:
        title = title[:2_000]

    return title, text, links


async def fetch_one(session: aiohttp.ClientSession, url: str, timeout_sec: int = 15) -> Optional[Tuple[str, str]]:
    if url.startswith("file://"):
        try:
            p = Path(url[7:])
            return url, p.read_text(encoding="utf-8", errors="ignore")
        except Exception:
            return None

    if aiohttp is None or session is None:
        def _fetch_sync() -> Optional[Tuple[str, str]]:
            try:
                req = Request(url, headers={"User-Agent": USER_AGENT})
                with urlopen(req, timeout=timeout_sec) as resp:
                    ctype = (resp.headers.get("Content-Type") or "").lower()
                    if "text/html" not in ctype and "application/xhtml+xml" not in ctype:
                        return None
                    body = resp.read().decode("utf-8", errors="ignore")
                    return url, body
            except Exception:
                return None

        return await asyncio.to_thread(_fetch_sync)

    try:
        async with session.get(url, timeout=timeout_sec, allow_redirects=True) as resp:
            if resp.status != 200:
                return None
            ctype = (resp.headers.get("Content-Type") or "").lower()
            if "text/html" not in ctype and "application/xhtml+xml" not in ctype:
                return None
            text = await resp.text(errors="ignore")
            return str(resp.url), text
    except Exception:
        return None


async def crawl(
    seeds: Sequence[str],
    db_path: str,
    max_pages: int = 10_000,
    concurrency: int = 200,
    same_domain_only: bool = False,
    min_text_len: int = 200,
) -> None:
    ensure_schema(db_path)

    frontier: asyncio.Queue[str] = asyncio.Queue()
    for s in seeds:
        frontier.put_nowait(normalize_url(s))

    visited: Set[str] = set()
    seed_domains = {urlparse(normalize_url(s)).netloc for s in seeds}
    done_count = 0

    conn = sqlite3.connect(db_path)
    conn.execute("PRAGMA journal_mode=WAL;")

    headers = {"User-Agent": USER_AGENT}
    if aiohttp is not None:
        timeout = aiohttp.ClientTimeout(total=20)
        connector = aiohttp.TCPConnector(limit=concurrency * 2, ssl=False)
        session_ctx = aiohttp.ClientSession(headers=headers, timeout=timeout, connector=connector)
    else:
        session_ctx = None

    async def _run_workers(session):
        semaphore = asyncio.Semaphore(concurrency)

        async def worker() -> None:
            nonlocal done_count
            while done_count < max_pages:
                try:
                    url = await asyncio.wait_for(frontier.get(), timeout=2)
                except asyncio.TimeoutError:
                    break

                if url in visited:
                    frontier.task_done()
                    continue

                visited.add(url)
                if same_domain_only and urlparse(url).netloc not in seed_domains:
                    frontier.task_done()
                    continue

                async with semaphore:
                    result = await fetch_one(session, url)

                if result is None:
                    frontier.task_done()
                    continue

                real_url, html = result
                title, text, out_links = extract_text_and_links(real_url, html)

                if len(text) < min_text_len:
                    frontier.task_done()
                    continue

                content_hash = hash_text(text[:50_000])
                url_hash = hash_text(real_url)

                # 去重策略：URL 唯一 + 正文 hash 去重
                cur = conn.execute("SELECT id FROM pages WHERE content_hash=? LIMIT 1", (content_hash,))
                dup = cur.fetchone()
                if dup:
                    frontier.task_done()
                    continue

                conn.execute(
                    """
                    INSERT OR IGNORE INTO pages(url, url_hash, title, text, content_hash, fetched_at)
                    VALUES(?,?,?,?,?,?)
                    """,
                    (real_url, url_hash, title, text, content_hash, time.time()),
                )
                conn.commit()

                row = conn.execute("SELECT id FROM pages WHERE url=?", (real_url,)).fetchone()
                if row:
                    pid = int(row[0])
                    vec = text_to_vector((title + "\n" + text)[:40_000])
                    conn.execute(
                        "INSERT OR REPLACE INTO embeddings(page_id, vector, dim) VALUES(?,?,?)",
                        (pid, blob_from_vec(vec), len(vec)),
                    )
                    conn.executemany(
                        "INSERT INTO links(src_id, dst_url) VALUES(?,?)",
                        [(pid, lk) for lk in out_links[:200]],
                    )
                    conn.commit()

                    done_count += 1
                    if done_count % 100 == 0:
                        print(f"[crawl] done={done_count} frontier={frontier.qsize()} visited={len(visited)}")

                for lk in out_links[:100]:
                    if lk not in visited:
                        frontier.put_nowait(lk)

                frontier.task_done()

        workers = [asyncio.create_task(worker()) for _ in range(concurrency)]
        await asyncio.gather(*workers)

    if session_ctx is None:
        await _run_workers(None)
    else:
        async with session_ctx as session:
            await _run_workers(session)

    conn.close()
    print(f"[crawl] finished. stored_pages={done_count}")


def fts_search(conn: sqlite3.Connection, query: str, topk: int = 20) -> List[Tuple[int, float]]:
    tokens = tokenize(query)
    if tokens:
        safe_query = " OR ".join(f'"{t.replace("\"", "\"\"")}"' for t in tokens[:16])
    else:
        safe_query = f'"{query.replace("\"", "\"\"")}"'

    rows = conn.execute(
        """
        SELECT rowid, bm25(pages_fts) AS score
        FROM pages_fts
        WHERE pages_fts MATCH ?
        ORDER BY score
        LIMIT ?
        """,
        (safe_query, topk),
    ).fetchall()
    # bm25 越小越好，统一转为越大越好的分数
    results: List[Tuple[int, float]] = []
    for rid, score in rows:
        results.append((int(rid), 1.0 / (1.0 + abs(float(score)))))
    return results


def vector_search(conn: sqlite3.Connection, query: str, topk: int = 20) -> List[Tuple[int, float]]:
    qv = text_to_vector(query)
    rows = conn.execute("SELECT page_id, vector, dim FROM embeddings").fetchall()
    scored: List[Tuple[int, float]] = []
    for pid, blob, dim in rows:
        vec = vec_from_blob(blob, int(dim))
        if len(vec) == 0:
            continue
        score = float(np.dot(qv, vec)) if np is not None else _dot(qv, vec)
        scored.append((int(pid), score))
    scored.sort(key=lambda x: x[1], reverse=True)
    return scored[:topk]


def graph_boost(conn: sqlite3.Connection, page_ids: Iterable[int]) -> Dict[int, float]:
    ids = list(page_ids)
    if not ids:
        return {}
    out_deg = Counter()
    in_deg = Counter()

    marks = ",".join(["?"] * len(ids))
    rows = conn.execute(
        f"SELECT src_id, dst_url FROM links WHERE src_id IN ({marks})",
        ids,
    ).fetchall()

    url_to_id = {
        int(row[0]): row[1]
        for row in conn.execute(
            f"SELECT id, url FROM pages WHERE id IN ({marks})",
            ids,
        ).fetchall()
    }
    reverse = {u: i for i, u in url_to_id.items()}

    for src, dst_url in rows:
        src = int(src)
        out_deg[src] += 1
        if dst_url in reverse:
            in_deg[reverse[dst_url]] += 1

    boost = {}
    for pid in ids:
        b = 0.2 * math.log1p(in_deg[pid]) + 0.1 * math.log1p(out_deg[pid])
        boost[pid] = b
    return boost


def hybrid_retrieve(db_path: str, query: str, topk: int = 10) -> List[Dict[str, object]]:
    conn = sqlite3.connect(db_path)

    fts = fts_search(conn, query, topk=topk * 5)
    vec = vector_search(conn, query, topk=topk * 5)

    score_map: Dict[int, float] = {}
    for pid, s in fts:
        score_map[pid] = score_map.get(pid, 0.0) + 0.6 * s
    for pid, s in vec:
        score_map[pid] = score_map.get(pid, 0.0) + 0.4 * max(0.0, s)

    boost = graph_boost(conn, score_map.keys())
    for pid, b in boost.items():
        score_map[pid] = score_map.get(pid, 0.0) + b

    ranked = sorted(score_map.items(), key=lambda x: x[1], reverse=True)[:topk]

    results: List[Dict[str, object]] = []
    for pid, score in ranked:
        row = conn.execute("SELECT url, title, text FROM pages WHERE id=?", (pid,)).fetchone()
        if not row:
            continue
        url, title, text = row
        snippet = re.sub(r"\s+", " ", text[:600])
        results.append({"id": pid, "score": round(score, 4), "url": url, "title": title, "snippet": snippet})

    conn.close()
    return results


def fake_agent_reasoning(query: str, docs: List[Dict[str, object]]) -> Dict[str, object]:
    """占位 Agent：生产中替换为真实 LLM + 工具调用链。"""
    citations = [d["url"] for d in docs]
    bullets = []
    for i, d in enumerate(docs[:5], 1):
        bullets.append(f"[{i}] {d['title'] or '(no-title)'} - {d['url']}")

    answer = (
        f"查询：{query}\n"
        f"基于 Top-{len(docs)} 相关网页，已完成粗粒度核查与汇总。\n"
        "候选证据：\n- " + "\n- ".join(bullets)
    )
    return {
        "query": query,
        "answer": answer,
        "citations": citations,
        "ts": time.time(),
    }


def cmd_crawl(args: argparse.Namespace) -> None:
    seeds = [s.strip() for s in Path(args.seeds_file).read_text(encoding="utf-8").splitlines() if s.strip()]
    asyncio.run(
        crawl(
            seeds=seeds,
            db_path=args.db,
            max_pages=args.max_pages,
            concurrency=args.concurrency,
            same_domain_only=args.same_domain_only,
            min_text_len=args.min_text_len,
        )
    )


def cmd_query(args: argparse.Namespace) -> None:
    hits = hybrid_retrieve(args.db, args.query, topk=args.topk)
    out = fake_agent_reasoning(args.query, hits)
    print(json.dumps({"hits": hits, "agent": out}, ensure_ascii=False, indent=2))


def cmd_stats(args: argparse.Namespace) -> None:
    conn = sqlite3.connect(args.db)
    pages = conn.execute("SELECT COUNT(*) FROM pages").fetchone()[0]
    emb = conn.execute("SELECT COUNT(*) FROM embeddings").fetchone()[0]
    links = conn.execute("SELECT COUNT(*) FROM links").fetchone()[0]
    conn.close()
    print(json.dumps({"pages": pages, "embeddings": emb, "links": links}, ensure_ascii=False, indent=2))


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description="1M 网页快速查看：抓取/清洗/检索/Top-K/Agent 一体化示例")
    sub = p.add_subparsers(required=True)

    c1 = sub.add_parser("crawl", help="从种子 URL 开始抓取并建索引")
    c1.add_argument("--seeds-file", required=True, help="每行一个种子 URL")
    c1.add_argument("--db", default="webindex.db")
    c1.add_argument("--max-pages", type=int, default=5000)
    c1.add_argument("--concurrency", type=int, default=200)
    c1.add_argument("--min-text-len", type=int, default=200)
    c1.add_argument("--same-domain-only", action="store_true")
    c1.set_defaults(func=cmd_crawl)

    c2 = sub.add_parser("query", help="混合检索 + Agent 汇总")
    c2.add_argument("--db", default="webindex.db")
    c2.add_argument("--query", required=True)
    c2.add_argument("--topk", type=int, default=10)
    c2.set_defaults(func=cmd_query)

    c3 = sub.add_parser("stats", help="查看索引规模")
    c3.add_argument("--db", default="webindex.db")
    c3.set_defaults(func=cmd_stats)

    return p


def main() -> None:
    parser = build_parser()
    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
