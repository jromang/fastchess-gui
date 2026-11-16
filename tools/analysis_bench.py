#!/usr/bin/env python3
"""Bench for the Board page's continuous analysis: which order of the positions, and whether replaying the previous
pass's principal variation backwards before a position, brings a game's analysis closest to a long search, for the
same total work.

Each game is analysed in passes of growing budget (4 times more at each pass), the engine's hash kept from one
position to the next and from one pass to the next, by three methods:

  forward      the positions from the start to the end (the program's analysis until now)
  backward     from the end to the start: the positions after it are in the hash when a position comes
  backward-pv  backward, and before each position its PV of the previous pass searched from its end back to it, with
               a quarter of the position's budget shared among those searches (three quarters for the position)

After each pass, every position's evaluation and best move are compared with the reference, one long search of each
position (backward, hash kept): the mean error of White's winning chances (the lichess formula), how often the best
move is the reference's, and how often a move's judgement (inaccuracy, mistake, blunder, as the program judges moves)
is the reference's. Budgets are nodes, so that the results do not depend on what else the machine runs; the
conclusions hold for a time per position as the program uses.

  tools/analysis_bench.py --engine ./stockfish --pgn games.pgn --games 6 --jobs 2 --out results.json

Needs python-chess. Runs each job under nice, the engines with 1 thread.
"""

import argparse
import json
import math
import multiprocessing
import os
import subprocess
import sys

import chess
import chess.pgn


def win_chance(cp):
    """The side's winning chances, 0 to 100, from its centipawns (lichess)."""
    return 50 + 50 * (2 / (1 + math.exp(-0.00368208 * cp)) - 1)


class Engine:
    def __init__(self, cmd, hash_mb):
        self.p = subprocess.Popen([cmd], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                                  text=True, bufsize=1, cwd=os.path.dirname(os.path.abspath(cmd)) or None)
        self.send("uci")
        self.wait("uciok")
        self.send("setoption name Threads value 1")
        self.send(f"setoption name Hash value {hash_mb}")
        self.ready()

    def send(self, line):
        self.p.stdin.write(line + "\n")
        self.p.stdin.flush()

    def wait(self, token):
        lines = []
        while True:
            line = self.p.stdout.readline()
            if not line:
                raise RuntimeError("the engine stopped")
            lines.append(line.rstrip("\n"))
            if line.startswith(token):
                return lines

    def ready(self):
        self.send("isready")
        self.wait("readyok")

    def new_game(self):
        self.send("ucinewgame")
        self.ready()

    def search(self, fen, moves, nodes):
        """(White's winning chances, best move, PV) of the position after `moves` from `fen`."""
        self.send(f"position fen {fen}" + (" moves " + " ".join(moves) if moves else ""))
        self.send(f"go nodes {nodes}")
        lines = self.wait("bestmove")
        best = lines[-1].split()[1]
        score, pv = None, []
        for line in lines:
            t = line.split()
            if not t or t[0] != "info" or "score" not in t or "pv" not in t:
                continue
            if "multipv" in t and t[t.index("multipv") + 1] != "1":
                continue
            bound = "lowerbound" in t or "upperbound" in t
            i = t.index("score")
            kind, value = t[i + 1], int(t[i + 2])
            if score is None or not bound:
                score = (kind, value)
                pv = t[t.index("pv") + 1:]
        if score is None:
            return None, best, []
        board = chess.Board(fen)
        for m in moves:
            board.push_uci(m)
        kind, value = score
        own = (100.0 if value > 0 else 0.0) if kind == "mate" else win_chance(value)
        white = own if board.turn == chess.WHITE else 100.0 - own
        return white, best, pv

    def close(self):
        try:
            self.send("quit")
            self.p.wait(timeout=5)
        except Exception:
            self.p.kill()


def judge(before, after):
    loss = before - after
    return 3 if loss >= 30 else 2 if loss >= 20 else 1 if loss >= 10 else 0


def judgements(game_moves, start_turn, evals):
    """Each move's judgement from the evaluations (White's chances) before and after it, as the program judges."""
    out = []
    turn = start_turn
    for i, move in enumerate(game_moves):
        a, b = evals[i], evals[i + 1]
        if a is None or b is None or a[0] is None or b[0] is None:
            out.append(None)
        elif move == a[1]:
            out.append(0)  # the engine's own choice
        else:
            mine = (lambda w: w) if turn == chess.WHITE else (lambda w: 100.0 - w)
            out.append(judge(mine(a[0]), mine(b[0])))
        turn = not turn
    return out


def analyse_game(job):
    os.nice(19)
    cmd, hash_mb, ref_nodes, passes, index, fen, moves = job
    n = len(moves)
    board = chess.Board(fen)
    over = []
    for m in moves:
        over.append(board.is_game_over())
        board.push_uci(m)
    over.append(board.is_game_over())
    positions = [p for p in range(n + 1) if not over[p]]

    def run(order, pv_replay, budgets):
        engine = Engine(cmd, hash_mb)
        engine.new_game()
        results, used = [], 0
        previous = {}
        for budget in budgets:
            evals = [None] * (n + 1)
            for p in (positions if order == "forward" else list(reversed(positions))):
                main = budget
                if pv_replay and p in previous and len(previous[p]) > 1:
                    line = previous[p][: min(len(previous[p]), 9)]
                    # The PV's positions from its end back to the position, the last move of the line excluded.
                    tips = []
                    b = chess.Board(fen)
                    for m in moves[:p]:
                        b.push_uci(m)
                    legal = []
                    for m in line[:-1]:
                        if chess.Move.from_uci(m) not in b.legal_moves:
                            break
                        b.push_uci(m)
                        legal.append(m)
                        if b.is_game_over():
                            break
                    if legal:
                        share = max(500, budget // 4 // len(legal))
                        for j in range(len(legal), 0, -1):
                            engine.search(fen, moves[:p] + legal[:j], share)
                            used += share
                        main = budget - share * len(legal)
                win, best, pv = engine.search(fen, moves[:p], max(1000, main))
                used += max(1000, main)
                evals[p] = (win, best)
                previous[p] = pv
            results.append({"budget": budget, "used": used, "evals": evals})
        engine.close()
        return results

    reference = run("backward", False, [ref_nodes])[0]["evals"]
    out = {"index": index, "plies": n, "reference": reference}
    for name, order, replay in (("forward", "forward", False), ("backward", "backward", False),
                                ("backward-pv", "backward", True)):
        out[name] = run(order, replay, passes)
    print(f"game {index}: {n} plies done", file=sys.stderr, flush=True)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--engine", required=True)
    ap.add_argument("--pgn", required=True)
    ap.add_argument("--games", type=int, default=6)
    ap.add_argument("--min-plies", type=int, default=60)
    ap.add_argument("--max-plies", type=int, default=160)
    ap.add_argument("--hash", type=int, default=64)
    ap.add_argument("--ref-nodes", type=int, default=2_000_000)
    ap.add_argument("--passes", default="20000,80000,320000")
    ap.add_argument("--jobs", type=int, default=2)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    games = []
    with open(args.pgn, encoding="utf-8", errors="replace") as f:
        while len(games) < args.games:
            g = chess.pgn.read_game(f)
            if g is None:
                break
            moves = [m.uci() for m in g.mainline_moves()]
            if args.min_plies <= len(moves) <= args.max_plies and g.headers.get("Result", "*") in ("1-0", "0-1"):
                games.append((g.board().fen(), moves))
    passes = [int(x) for x in args.passes.split(",")]
    jobs = [(args.engine, args.hash, args.ref_nodes, passes, i, fen, moves) for i, (fen, moves) in enumerate(games)]
    with multiprocessing.Pool(args.jobs) as pool:
        results = pool.map(analyse_game, jobs)
    json.dump({"engine": args.engine, "passes": passes, "ref_nodes": args.ref_nodes, "games": results},
              open(args.out, "w"))

    # The summary: per method and pass, the errors against the reference over every position and move.
    print(f"{len(results)} games, reference {args.ref_nodes} nodes per position")
    print(f"{'method':<12} {'pass':>8} {'nodes/pos':>10} {'win err':>8} {'best %':>7} {'judged %':>9} {'mistakes':>9}")
    for name in ("forward", "backward", "backward-pv"):
        for k, budget in enumerate(passes):
            err = agree = total = jagree = jtotal = found = ref_mistakes = 0
            used = positions = 0
            for r, (fen, moves) in zip(results, games):
                ref = r["reference"]
                mine = r[name][k]["evals"]
                used += r[name][k]["used"]
                for p in range(len(ref)):
                    if ref[p] is None or mine[p] is None or ref[p][0] is None or mine[p][0] is None:
                        continue
                    positions += 1
                    err += abs(ref[p][0] - mine[p][0])
                    agree += ref[p][1] == mine[p][1]
                    total += 1
                turn = chess.Board(fen).turn
                rj, mj = judgements(moves, turn, ref), judgements(moves, turn, mine)
                for a, b in zip(rj, mj):
                    if a is None or b is None:
                        continue
                    jtotal += 1
                    jagree += a == b
                    if a >= 2:
                        ref_mistakes += 1
                        found += b >= 2
            print(f"{name:<12} {budget:>8} {used / max(1, positions):>10.0f} {err / max(1, total):>8.2f} "
                  f"{100 * agree / max(1, total):>7.1f} {100 * jagree / max(1, jtotal):>9.1f} "
                  f"{found:>4}/{ref_mistakes:<4}")


if __name__ == "__main__":
    main()
