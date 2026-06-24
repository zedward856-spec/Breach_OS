import { NextResponse } from 'next/server';
import { getUsers, getLeaderboard } from '@/lib/kv';

export async function POST(req: Request) {
    try {
        const { password } = await req.json();
        
        // Master admin override code
        if (password !== 'admin2077') {
            return NextResponse.json({ error: 'Unauthorized' }, { status: 401 });
        }

        const users = await getUsers();
        const leaderboard = await getLeaderboard();

        return NextResponse.json({ success: true, users, leaderboard });
    } catch (e) {
        return NextResponse.json({ error: 'Server error' }, { status: 500 });
    }
}
