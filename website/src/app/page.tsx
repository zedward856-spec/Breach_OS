"use client";
import { useEffect, useState } from 'react';

type ScoreEntry = {
  username: string;
  score: number;
  date: string;
};

export default function Leaderboard() {
  const [scores, setScores] = useState<ScoreEntry[]>([]);
  const [loading, setLoading] = useState(true);

  const fetchScores = async () => {
    try {
      const res = await fetch('/api/leaderboard');
      const json = await res.json();
      setScores(json.data || []);
    } catch (e) {
      console.error("Failed to fetch leaderboard");
    } finally {
      setLoading(false);
    }
  };

  useEffect(() => {
    fetchScores();
    const interval = setInterval(fetchScores, 5000); // Poll every 5s
    return () => clearInterval(interval);
  }, []);

  return (
    <>
      <div className="scanline" />
      <div className="container">
        <h1 className="title-glitch">GLOBAL_NETWORK_NODE</h1>
        <div className="board-wrapper">
          <div className="table-header">
            <div>RANK</div>
            <div>OPERATIVE</div>
            <div>CREDIT_RATING</div>
          </div>

          {loading ? (
            <div className="table-row">
              <div style={{ gridColumn: 'span 3', textAlign: 'center', color: 'var(--cp-dim)' }}>
                [ CONNECTING TO DATABANK... ]
              </div>
            </div>
          ) : scores.length === 0 ? (
            <div className="table-row">
              <div style={{ gridColumn: 'span 3', textAlign: 'center', color: 'var(--cp-dim)' }}>
                [ NO NETWORK ACTIVITY DETECTED ]
              </div>
            </div>
          ) : (
            scores.map((entry, idx) => (
              <div className="table-row" key={idx}>
                <div className="rank-text">#{idx + 1}</div>
                <div>{entry.username}</div>
                <div className="score-text">{entry.score}</div>
              </div>
            ))
          )}
        </div>
        <div style={{ marginTop: '20px', textAlign: 'center', color: 'var(--cp-dim)', fontSize: '0.8rem', letterSpacing: '2px' }}>
          VERSION: v5.0sound
        </div>
      </div>
    </>
  );
}
