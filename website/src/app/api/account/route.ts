import { NextResponse } from 'next/server';
import fs from 'fs';
import path from 'path';

const usersFile = path.join(process.cwd(), 'data', 'users.json');
const leaderboardFile = path.join(process.cwd(), 'data', 'leaderboard.json');

export async function POST(req: Request) {
    try {
        const body = await req.json();
        const { action, username, password, newUsername, newPassword } = body;

        let users: Record<string, string> = {};
        if (fs.existsSync(usersFile)) {
            users = JSON.parse(fs.readFileSync(usersFile, 'utf-8'));
        }

        if (!users[username] || users[username] !== password) {
            return NextResponse.json({ error: 'Unauthorized' }, { status: 401 });
        }

        if (action === 'get_stats') {
            let highScore = 0;
            if (fs.existsSync(leaderboardFile)) {
                const leaderboard = JSON.parse(fs.readFileSync(leaderboardFile, 'utf-8'));
                const userScores = leaderboard.filter((entry: any) => entry.username === username);
                if (userScores.length > 0) {
                    highScore = Math.max(...userScores.map((e: any) => e.score));
                }
            }
            return NextResponse.json({ highScore });
        }

        if (action === 'update_account') {
            const finalUsername = newUsername || username;
            const finalPassword = newPassword || password;

            if (newUsername && newUsername !== username) {
                if (users[newUsername]) {
                    return NextResponse.json({ error: 'Username taken' }, { status: 400 });
                }
                
                if (fs.existsSync(leaderboardFile)) {
                    const leaderboard = JSON.parse(fs.readFileSync(leaderboardFile, 'utf-8'));
                    let modified = false;
                    leaderboard.forEach((entry: any) => {
                        if (entry.username === username) {
                            entry.username = newUsername;
                            modified = true;
                        }
                    });
                    if (modified) fs.writeFileSync(leaderboardFile, JSON.stringify(leaderboard, null, 2));
                }
                delete users[username];
            }

            users[finalUsername] = finalPassword;
            fs.writeFileSync(usersFile, JSON.stringify(users, null, 2));

            return NextResponse.json({ success: true, message: 'Account updated' });
        }

        return NextResponse.json({ error: 'Invalid action' }, { status: 400 });
    } catch (e) {
        return NextResponse.json({ error: 'Server error' }, { status: 500 });
    }
}
