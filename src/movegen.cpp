/*
 * Polaris, a UCI chess engine
 * Copyright (C) 2023 Ciekce
 *
 * Polaris is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Polaris is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Polaris. If not, see <https://www.gnu.org/licenses/>.
 */

#include "movegen.h"

#include <array>
#include <algorithm>

#include "attacks/attacks.h"
#include "rays.h"
#include "eval/material.h"
#include "util/bitfield.h"
#include "opts.h"

namespace polaris
{
	namespace
	{
		inline void pushStandards(ScoredMoveList &dst, i32 offset, Bitboard board)
		{
			while (!board.empty())
			{
				const auto dstSquare = board.popLowestSquare();
				const auto srcSquare = static_cast<Square>(static_cast<i32>(dstSquare) - offset);

				dst.push({Move::standard(srcSquare, dstSquare), 0});
			}
		}

		inline void pushStandards(ScoredMoveList &dst, Square srcSquare, Bitboard board)
		{
			while (!board.empty())
			{
				const auto dstSquare = board.popLowestSquare();
				dst.push({Move::standard(srcSquare, dstSquare), 0});
			}
		}

		inline void pushQueenPromotions(ScoredMoveList &noisy, i32 offset, Bitboard board)
		{
			while (!board.empty())
			{
				const auto dstSquare = board.popLowestSquare();
				const auto srcSquare = static_cast<Square>(static_cast<i32>(dstSquare) - offset);

				noisy.push({Move::promotion(srcSquare, dstSquare, BasePiece::Queen), 0});
			}
		}

		inline void pushUnderpromotions(ScoredMoveList &quiet, i32 offset, Bitboard board)
		{
			while (!board.empty())
			{
				const auto dstSquare = board.popLowestSquare();
				const auto srcSquare = static_cast<Square>(static_cast<i32>(dstSquare) - offset);

				quiet.push({Move::promotion(srcSquare, dstSquare, BasePiece::Knight), 0});

				if (g_opts.underpromotions)
				{
					quiet.push({Move::promotion(srcSquare, dstSquare, BasePiece::Rook), 0});
					quiet.push({Move::promotion(srcSquare, dstSquare, BasePiece::Bishop), 0});
				}
			}
		}

		inline void pushCastling(ScoredMoveList &dst, Square srcSquare, Square dstSquare)
		{
			dst.push({Move::castling(srcSquare, dstSquare), 0});
		}

		inline void pushEnPassants(ScoredMoveList &noisy, i32 offset, Bitboard board)
		{
			while (!board.empty())
			{
				const auto dstSquare = board.popLowestSquare();
				const auto srcSquare = static_cast<Square>(static_cast<i32>(dstSquare) - offset);

				noisy.push({Move::enPassant(srcSquare, dstSquare), 0});
			}
		}

		template <Color Us>
		void generatePawnsNoisy_(ScoredMoveList &noisy, const Position &pos, Bitboard dstMask)
		{
			constexpr auto Them = oppColor(Us);

			constexpr auto PromotionRank = boards::promotionRank<Us>();

			constexpr auto ForwardOffset = offsets::up<Us>();
			constexpr auto LeftOffset = offsets::upLeft<Us>();
			constexpr auto RightOffset = offsets::upRight<Us>();

			const auto &boards = pos.boards();

			const auto theirs = boards.occupancy<Them>();

			const auto forwardDstMask = dstMask & PromotionRank & ~theirs;

			const auto pawns = boards.pawns<Us>();

			const auto leftAttacks = pawns.template shiftUpLeftRelative<Us>() & dstMask;
			const auto rightAttacks = pawns.template shiftUpRightRelative<Us>() & dstMask;

			pushQueenPromotions(noisy, LeftOffset,   leftAttacks & theirs & PromotionRank);
			pushQueenPromotions(noisy, RightOffset, rightAttacks & theirs & PromotionRank);

			const auto forwards = pawns.template shiftUpRelative<Us>() & forwardDstMask;
			pushQueenPromotions(noisy, ForwardOffset, forwards);

			pushStandards(noisy,  LeftOffset,  leftAttacks & theirs & ~PromotionRank);
			pushStandards(noisy, RightOffset, rightAttacks & theirs & ~PromotionRank);

			if (pos.enPassant() != Square::None)
			{
				const auto epMask = Bitboard::fromSquare(pos.enPassant());

				pushEnPassants(noisy,  LeftOffset,  leftAttacks & epMask);
				pushEnPassants(noisy, RightOffset, rightAttacks & epMask);
			}
		}

		inline void generatePawnsNoisy(ScoredMoveList &noisy, const Position &pos, Bitboard dstMask)
		{
			if (pos.toMove() == Color::Black)
				generatePawnsNoisy_<Color::Black>(noisy, pos, dstMask);
			else generatePawnsNoisy_<Color::White>(noisy, pos, dstMask);
		}

		template <Color Us>
		void generatePawnsQuiet_(ScoredMoveList &quiet, const PositionBoards &boards, Bitboard dstMask, Bitboard occ)
		{
			constexpr auto Them = oppColor(Us);

			constexpr auto PromotionRank = boards::promotionRank<Us>();
			constexpr auto ThirdRank = boards::rank<Us>(2);

			const auto ForwardOffset = offsets::up<Us>();
			const auto DoubleOffset = ForwardOffset * 2;

			constexpr auto  LeftOffset = offsets::upLeft <Us>();
			constexpr auto RightOffset = offsets::upRight<Us>();

			const auto theirs = boards.occupancy<Them>();

			const auto forwardDstMask = dstMask & ~theirs;

			const auto pawns = boards.pawns<Us>();

			const auto  leftAttacks = pawns.template shiftUpLeftRelative <Us>() & dstMask;
			const auto rightAttacks = pawns.template shiftUpRightRelative<Us>() & dstMask;

			pushUnderpromotions(quiet,  LeftOffset,  leftAttacks & theirs & PromotionRank);
			pushUnderpromotions(quiet, RightOffset, rightAttacks & theirs & PromotionRank);

			auto forwards = pawns.template shiftUpRelative<Us>() & ~occ;

			auto singles = forwards & forwardDstMask;
			pushUnderpromotions(quiet, ForwardOffset, singles & PromotionRank);
			singles &= ~PromotionRank;

			forwards &= ThirdRank;
			const auto doubles = forwards.template shiftUpRelative<Us>() & forwardDstMask;

			pushStandards(quiet,  DoubleOffset, doubles);
			pushStandards(quiet, ForwardOffset, singles);
		}

		inline void generatePawnsQuiet(ScoredMoveList &quiet, const Position &pos, Bitboard dstMask, Bitboard occ)
		{
			if (pos.toMove() == Color::Black)
				generatePawnsQuiet_<Color::Black>(quiet, pos.boards(), dstMask, occ);
			else generatePawnsQuiet_<Color::White>(quiet, pos.boards(), dstMask, occ);
		}

		template <BasePiece Piece, const std::array<Bitboard, 64> &Attacks>
		inline void precalculated(ScoredMoveList &dst, const Position &pos, Bitboard dstMask)
		{
			const auto us = pos.toMove();

			auto pieces = pos.boards().forPiece(Piece, us);
			while (!pieces.empty())
			{
				const auto srcSquare = pieces.popLowestSquare();
				const auto attacks = Attacks[static_cast<usize>(srcSquare)];

				pushStandards(dst, srcSquare, attacks & dstMask);
			}
		}

		void generateKnights(ScoredMoveList &dst, const Position &pos, Bitboard dstMask)
		{
			precalculated<BasePiece::Knight, attacks::KnightAttacks>(dst, pos, dstMask);
		}

		inline void generateFrcCastling(ScoredMoveList &dst, const Position &pos, Bitboard occupancy,
			Square king, Square kingDst, Square rook, Square rookDst)
		{
			const auto toKingDst = rayBetween(king, kingDst);
			const auto toRook = rayBetween(king, rook);

			const auto occ = occupancy ^ squareBit(king) ^ squareBit(rook);

			if ((occ & (toKingDst | toRook | squareBit(kingDst) | squareBit(rookDst))).empty()
				&& !pos.anyAttacked(toKingDst | squareBit(kingDst), pos.opponent()))
				pushCastling(dst, king, rook);
		}

		template <bool Castling>
		void generateKings(ScoredMoveList &dst, const Position &pos, Bitboard dstMask)
		{
			precalculated<BasePiece::King, attacks::KingAttacks>(dst, pos, dstMask);

			if constexpr (Castling)
			{
				if (!pos.isCheck())
				{
					const auto &castlingRooks = pos.castlingRooks();
					const auto occupancy = pos.boards().occupancy();

					// this branch is cheaper than the extra checks the chess960 castling movegen does
					if (g_opts.chess960)
					{
						if (pos.toMove() == Color::Black)
						{
							if (castlingRooks.blackShort != Square::None)
								generateFrcCastling(dst, pos, occupancy,
									pos.blackKing(), Square::G8,
									castlingRooks.blackShort, Square::F8);
							if (castlingRooks.blackLong != Square::None)
								generateFrcCastling(dst, pos, occupancy,
									pos.blackKing(), Square::C8,
									castlingRooks.blackLong, Square::D8);
						}
						else
						{
							if (castlingRooks.whiteShort != Square::None)
								generateFrcCastling(dst, pos, occupancy,
									pos.whiteKing(), Square::G1,
									castlingRooks.whiteShort, Square::F1);
							if (castlingRooks.whiteLong != Square::None)
								generateFrcCastling(dst, pos, occupancy,
									pos.whiteKing(), Square::C1,
									castlingRooks.whiteLong, Square::D1);
						}
					}
					else
					{
						if (pos.toMove() == Color::Black)
						{
							if (castlingRooks.blackShort != Square::None
								&& (occupancy & U64(0x6000000000000000)).empty()
								&& !pos.isAttacked(Square::F8, Color::White))
								pushCastling(dst, pos.blackKing(), Square::H8);

							if (castlingRooks.blackLong != Square::None
								&& (occupancy & U64(0x0E00000000000000)).empty()
								&& !pos.isAttacked(Square::D8, Color::White))
								pushCastling(dst, pos.blackKing(), Square::A8);
						}
						else
						{
							if (castlingRooks.whiteShort != Square::None
								&& (occupancy & U64(0x0000000000000060)).empty()
								&& !pos.isAttacked(Square::F1, Color::Black))
								pushCastling(dst, pos.whiteKing(), Square::H1);

							if (castlingRooks.whiteLong != Square::None
								&& (occupancy & U64(0x000000000000000E)).empty()
								&& !pos.isAttacked(Square::D1, Color::Black))
								pushCastling(dst, pos.whiteKing(), Square::A1);
						}
					}
				}
			}
		}

		void generateSliders(ScoredMoveList &dst, const Position &pos, Bitboard dstMask)
		{
			const auto &boards = pos.boards();

			const auto us = pos.toMove();
			const auto them = oppColor(us);

			const auto ours = boards.forColor(us);
			const auto theirs = boards.forColor(them);

			const auto occupancy = ours | theirs;

			const auto queens = boards.queens(us);

			auto rooks = queens | boards.rooks(us);
			auto bishops = queens | boards.bishops(us);

			while (!rooks.empty())
			{
				const auto src = rooks.popLowestSquare();
				const auto attacks = attacks::getRookAttacks(src, occupancy);

				pushStandards(dst, src, attacks & dstMask);
			}

			while (!bishops.empty())
			{
				const auto src = bishops.popLowestSquare();
				const auto attacks = attacks::getBishopAttacks(src, occupancy);

				pushStandards(dst, src, attacks & dstMask);
			}
		}
	}

	void generateNoisy(ScoredMoveList &noisy, const Position &pos)
	{
		const auto &boards = pos.boards();

		const auto us = pos.toMove();
		const auto them = oppColor(us);

		const auto ours = boards.forColor(us);

		const auto kingDstMask = boards.forColor(them);

		auto dstMask = kingDstMask;

		Bitboard epMask{};
		Bitboard epPawn{};

		if (pos.enPassant() != Square::None)
		{
			epMask = Bitboard::fromSquare(pos.enPassant());
			epPawn = us == Color::Black ? epMask.shiftUp() : epMask.shiftDown();
		}

		// queen promotions are noisy
		const auto promos = ~ours & (us == Color::Black ? boards::Rank1 : boards::Rank8);

		auto pawnDstMask = kingDstMask | epMask | promos;

		if (pos.isCheck())
		{
			if (pos.checkers().multiple())
			{
				generateKings<false>(noisy, pos, kingDstMask);
				return;
			}

			dstMask = pos.checkers();

			pawnDstMask = kingDstMask | (promos & rayBetween(pos.king(us), pos.checkers().lowestSquare()));

			// pawn that just moved is the checker
			if (!(pos.checkers() & epPawn).empty())
				pawnDstMask |= epMask;
		}

		generateSliders(noisy, pos, dstMask);
		generatePawnsNoisy(noisy, pos, pawnDstMask);
		generateKnights(noisy, pos, dstMask);
		generateKings<false>(noisy, pos, kingDstMask);
	}

	void generateQuiet(ScoredMoveList &quiet, const Position &pos)
	{
		const auto &boards = pos.boards();

		const auto us = pos.toMove();
		const auto them = oppColor(us);

		const auto ours = boards.forColor(us);
		const auto theirs = boards.forColor(them);

		const auto kingDstMask = ~(ours | theirs);

		auto dstMask = kingDstMask;
		// for underpromotions
		auto pawnDstMask = kingDstMask;

		if (pos.isCheck())
		{
			if (pos.checkers().multiple())
			{
				generateKings<false>(quiet, pos, kingDstMask);
				return;
			}

			pawnDstMask = dstMask = rayBetween(pos.king(us), pos.checkers().lowestSquare());

			pawnDstMask |= pos.checkers() & boards::promotionRank(us);
		}
		else pawnDstMask |= boards::promotionRank(us);

		generateSliders(quiet, pos, dstMask);
		generatePawnsQuiet(quiet, pos, pawnDstMask, ours | theirs);
		generateKnights(quiet, pos, dstMask);
		generateKings<true>(quiet, pos, kingDstMask);
	}

	void generateAll(ScoredMoveList &dst, const Position &pos)
	{
		const auto &boards = pos.boards();

		const auto us = pos.toMove();

		const auto kingDstMask = ~boards.forColor(pos.toMove());

		auto dstMask = kingDstMask;

		Bitboard epMask{};
		Bitboard epPawn{};

		if (pos.enPassant() != Square::None)
		{
			epMask = Bitboard::fromSquare(pos.enPassant());
			epPawn = pos.toMove() == Color::Black ? epMask.shiftUp() : epMask.shiftDown();
		}

		auto pawnDstMask = kingDstMask;

		if (pos.isCheck())
		{
			if (pos.checkers().multiple())
			{
				generateKings<false>(dst, pos, kingDstMask);
				return;
			}

			pawnDstMask = dstMask = pos.checkers()
				| rayBetween(pos.king(us), pos.checkers().lowestSquare());

			if (!(pos.checkers() & epPawn).empty())
				pawnDstMask |= epMask;
		}

		generateSliders(dst, pos, dstMask);
		generatePawnsNoisy(dst, pos, pawnDstMask);
		generatePawnsQuiet(dst, pos, dstMask, boards.occupancy());
		generateKnights(dst, pos, dstMask);
		generateKings<true>(dst, pos, kingDstMask);
	}
}
