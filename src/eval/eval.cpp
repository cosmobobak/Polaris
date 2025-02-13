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

#include "eval.h"

#include <array>
#include <iostream>

#include "../pretty.h"
#include "../attacks/attacks.h"
#include "../rays.h"

namespace polaris::eval
{
	namespace
	{
#define S(Mg, Eg) TaperedScore{(Mg), (Eg)}

		// pawn structure
		constexpr auto DoubledPawn = S(-18, -25);
		// idea from weiss
		constexpr auto DoubledGappedPawn = S(-4, -18);
		constexpr auto PawnDefender = S(17, 14);
		constexpr auto OpenPawn = S(-11, -7);

		constexpr auto PawnPhalanx = std::array {
			S(0, 0), S(3, 5), S(22, 10), S(25, 25), S(44, 61), S(118, 136), S(23, 259)
		};

		constexpr auto Passer = std::array {
			S(0, 0), S(0, 7), S(-4, 14), S(-13, 45), S(12, 66), S(8, 138), S(48, 152)
		};

		constexpr auto DefendedPasser = std::array {
			S(0, 0), S(0, 0), S(4, -9), S(5, -11), S(8, 0), S(33, 15), S(154, -12)
		};

		constexpr auto BlockedPasser = std::array {
			S(0, 0), S(-9, -3), S(-9, 3), S(-5, -8), S(-13, -24), S(5, -87), S(29, -138)
		};

		constexpr auto CandidatePasser = std::array {
			S(0, 0), S(7, -3), S(1, 0), S(3, 12), S(20, 16), S(46, 60), S(0, 0)
		};

		constexpr auto DoubledPasser = S(17, -26);
		constexpr auto PasserHelper = S(-8, 13);

		// pawns
		constexpr auto PawnAttackingMinor = S(52, 17);
		constexpr auto PawnAttackingRook = S(98, -31);
		constexpr auto PawnAttackingQueen = S(57, -16);

		constexpr auto PasserSquareRule = S(12, 102);

		// minors
		constexpr auto MinorBehindPawn = S(5, 18);

		constexpr auto MinorAttackingRook = S(40, 0);
		constexpr auto MinorAttackingQueen = S(27, 3);

		// knights
		constexpr auto KnightOutpost = S(25, 16);

		// bishops
		constexpr auto BishopPair = S(26, 59);

		// rooks
		constexpr auto RookOnOpenFile = S(41, 2);
		constexpr auto RookOnSemiOpenFile = S(15, 9);
		constexpr auto RookSupportingPasser = S(17, 14);
		constexpr auto RookAttackingQueen = S(55, -23);

		// queens

		// kings
		constexpr auto KingOnOpenFile = S(-71, 2);
		constexpr auto KingOnSemiOpenFile = S(-30, 18);

		// mobility
		constexpr auto KnightMobility = std::array {
			S(-42, -12), S(-23, -8), S(-12, -5), S(-8, 0), S(3, 3), S(8, 11), S(16, 10), S(20, 9),
			S(36, -8)
		};

		constexpr auto BishopMobility = std::array {
			S(-53, 5), S(-38, -13), S(-26, -23), S(-18, -16), S(-9, -8), S(-5, 0), S(0, 7), S(3, 9),
			S(2, 13), S(11, 9), S(21, 3), S(46, 0), S(7, 24), S(58, -10)
		};

		constexpr auto RookMobility = std::array {
			S(-42, -38), S(-29, -15), S(-23, -15), S(-18, -11), S(-17, -7), S(-11, -4), S(-9, 2), S(-4, 4),
			S(5, 7), S(11, 9), S(14, 12), S(23, 14), S(25, 18), S(42, 11), S(34, 11)
		};

		constexpr auto QueenMobility = std::array {
			S(-31, 63), S(-31, 222), S(-32, 89), S(-33, 53), S(-31, 49), S(-24, -23), S(-20, -58), S(-17, -68),
			S(-14, -66), S(-8, -73), S(-7, -59), S(-3, -49), S(-4, -45), S(4, -40), S(5, -29), S(0, -14),
			S(0, -4), S(16, -18), S(12, -5), S(27, -9), S(33, -5), S(64, -19), S(44, -3), S(83, -12),
			S(35, 4), S(41, 0), S(-42, 62), S(-66, 57)
		};
#undef S

		template <Color Us>
		consteval std::array<Bitboard, 64> generateAntiPasserMasks()
		{
			std::array<Bitboard, 64> dst{};

			for (u32 i = 0; i < 64; ++i)
			{
				dst[i] = Bitboard::fromSquare(static_cast<Square>(i));
				dst[i] |= dst[i].shiftLeft() | dst[i].shiftRight();

				dst[i] = dst[i].template shiftUpRelative<Us>().template fillUpRelative<Us>();
			}

			return dst;
		}

		template <Color Us>
		constexpr auto AntiPasserMasks = generateAntiPasserMasks<Us>();

		template <Color Us>
		consteval std::array<Bitboard, 64> generatePawnHelperMasks()
		{
			std::array<Bitboard, 64> dst{};

			for (u32 i = 0; i < 64; ++i)
			{
				dst[i] = Bitboard::fromSquare(static_cast<Square>(i));
				dst[i] = dst[i].shiftLeft() | dst[i].shiftRight();

				dst[i] = dst[i].template shiftDownRelative<Us>().template fillDownRelative<Us>();
			}

			return dst;
		}

		template <Color Us>
		constexpr auto PawnHelperMasks = generatePawnHelperMasks<Us>();

		class Evaluator
		{
		public:
			explicit Evaluator(const Position &pos, PawnCache *pawnCache);
			~Evaluator() = default;

			[[nodiscard]] inline auto eval() const { return m_final; }
			void printEval() const;

		private:
			struct EvalData
			{
				Bitboard pawnAttacks;

				Bitboard semiOpen;
				Bitboard available;

				Bitboard passers;

				TaperedScore pawnStructure{};

				TaperedScore pawns{};
				TaperedScore knights{};
				TaperedScore bishops{};
				TaperedScore rooks{};
				TaperedScore queens{};
				TaperedScore kings{};

				TaperedScore mobility{};

				TaperedScore hanging{};
				TaperedScore pinned{};

				TaperedScore kingSafety{};
			};

			template <Color Us>
			void evalPawnStructure   (const Position &pos, EvalData &ours, const EvalData &theirs);
			template <Color Us>
			void evalPawns           (const Position &pos, EvalData &ours, const EvalData &theirs);
			template <Color Us>
			void evalKnights         (const Position &pos, EvalData &ours, const EvalData &theirs);
			template <Color Us>
			void evalBishops         (const Position &pos, EvalData &ours, const EvalData &theirs);
			template <Color Us>
			void evalRooks           (const Position &pos, EvalData &ours, const EvalData &theirs);
			template <Color Us>
			void evalQueens          (const Position &pos, EvalData &ours, const EvalData &theirs);
			template <Color Us>
			void evalKing            (const Position &pos, EvalData &ours, const EvalData &theirs);
			template <Color Us>
			void evalHangingAndPinned(const Position &pos, EvalData &ours, const EvalData &theirs);
			template <Color Us>
			void evalKingSafety      (const Position &pos, EvalData &ours, const EvalData &theirs);

			bool m_cachedPawnStructureEval{false};

			const Position &m_pos;

			EvalData m_blackData{};
			EvalData m_whiteData{};

			Bitboard m_openFiles{};

			TaperedScore m_total{};
			Score m_final;
		};

		Evaluator::Evaluator(const Position &pos, PawnCache *pawnCache)
			: m_pos{pos}
		{
			const auto &boards = pos.boards();

			const auto blackPawns = boards.blackPawns();
			const auto whitePawns = boards.whitePawns();

			const auto blackPawnLeftAttacks = blackPawns.shiftDownLeft();
			const auto blackPawnRightAttacks = blackPawns.shiftDownRight();

			m_blackData.pawnAttacks = blackPawnLeftAttacks | blackPawnRightAttacks;

			const auto whitePawnLeftAttacks = whitePawns.shiftUpLeft();
			const auto whitePawnRightAttacks = whitePawns.shiftUpRight();

			m_whiteData.pawnAttacks = whitePawnLeftAttacks | whitePawnRightAttacks;

			m_blackData.semiOpen = ~blackPawns.fillFile();
			m_whiteData.semiOpen = ~whitePawns.fillFile();

			m_blackData.available = ~(boards.blackOccupancy() | m_whiteData.pawnAttacks);
			m_whiteData.available = ~(boards.whiteOccupancy() | m_blackData.pawnAttacks);

			m_openFiles = m_blackData.semiOpen & m_whiteData.semiOpen;

			m_total = pos.material();

			if (pawnCache)
			{
				auto &cacheEntry = pawnCache->probe(pos.pawnKey());

				if (cacheEntry.key == pos.pawnKey())
				{
					m_whiteData.pawnStructure = cacheEntry.eval;

					m_cachedPawnStructureEval = true;

					m_blackData.passers = cacheEntry.passers & boards.blackOccupancy();
					m_whiteData.passers = cacheEntry.passers & boards.whiteOccupancy();
				}
				else
				{
					evalPawnStructure<Color::Black>(pos, m_blackData, m_whiteData);
					evalPawnStructure<Color::White>(pos, m_whiteData, m_blackData);

					cacheEntry.key = pos.pawnKey();
					cacheEntry.eval = m_whiteData.pawnStructure - m_blackData.pawnStructure;
					cacheEntry.passers = m_blackData.passers | m_whiteData.passers;
				}
			}
			else
			{
				evalPawnStructure<Color::Black>(pos, m_blackData, m_whiteData);
				evalPawnStructure<Color::White>(pos, m_whiteData, m_blackData);
			}

			evalPawns<Color::Black>(pos, m_blackData, m_whiteData);
			evalPawns<Color::White>(pos, m_whiteData, m_blackData);

			evalKnights<Color::Black>(pos, m_blackData, m_whiteData);
			evalKnights<Color::White>(pos, m_whiteData, m_blackData);

			evalBishops<Color::Black>(pos, m_blackData, m_whiteData);
			evalBishops<Color::White>(pos, m_whiteData, m_blackData);

			evalRooks<Color::Black>(pos, m_blackData, m_whiteData);
			evalRooks<Color::White>(pos, m_whiteData, m_blackData);

			evalQueens<Color::Black>(pos, m_blackData, m_whiteData);
			evalQueens<Color::White>(pos, m_whiteData, m_blackData);

			evalKing<Color::Black>(pos, m_blackData, m_whiteData);
			evalKing<Color::White>(pos, m_whiteData, m_blackData);

			evalHangingAndPinned<Color::Black>(pos, m_blackData, m_whiteData);
			evalHangingAndPinned<Color::White>(pos, m_whiteData, m_blackData);

			evalKingSafety<Color::Black>(pos, m_blackData, m_whiteData);
			evalKingSafety<Color::White>(pos, m_whiteData, m_blackData);

			m_total += m_whiteData.pawnStructure - m_blackData.pawnStructure;

			m_total += m_whiteData.pawns         - m_blackData.pawns;
			m_total += m_whiteData.knights       - m_blackData.knights;
			m_total += m_whiteData.bishops       - m_blackData.bishops;
			m_total += m_whiteData.rooks         - m_blackData.rooks;
			m_total += m_whiteData.queens        - m_blackData.queens;
			m_total += m_whiteData.kings         - m_blackData.kings;

			m_total += m_whiteData.mobility      - m_blackData.mobility;

			m_total += m_whiteData.hanging       - m_blackData.hanging;
			m_total += m_whiteData.pinned        - m_blackData.pinned;

			m_total += m_whiteData.kingSafety    - m_blackData.kingSafety;

			m_final = pos.interpScore(m_total);

			m_final = (m_final * (200 - pos.halfmove())) / 200;

			if (pos.isLikelyDrawn())
				m_final /= 8;
		}

		void Evaluator::printEval() const
		{
			std::cout <<   "      Material: ";
			printScore(std::cout, m_pos.material());

			if (m_cachedPawnStructureEval)
			{
				std::cout << "\nPawn structure: ";
				printScore(std::cout, m_whiteData.pawnStructure);
				std::cout << " (cached)";
			}
			else
			{
				std::cout << "\nPawn structure: ";
				printScore(std::cout, m_whiteData.pawnStructure);
				std::cout << " - ";
				printScore(std::cout, m_blackData.pawnStructure);
			}

			std::cout << "\n         Pawns: ";
			printScore(std::cout, m_whiteData.pawns);
			std::cout << " - ";
			printScore(std::cout, m_blackData.pawns);

			std::cout << "\n       Knights: ";
			printScore(std::cout, m_whiteData.knights);
			std::cout << " - ";
			printScore(std::cout, m_blackData.knights);

			std::cout << "\n       Bishops: ";
			printScore(std::cout, m_whiteData.bishops);
			std::cout << " - ";
			printScore(std::cout, m_blackData.bishops);

			std::cout << "\n         Rooks: ";
			printScore(std::cout, m_whiteData.rooks);
			std::cout << " - ";
			printScore(std::cout, m_blackData.rooks);

			std::cout << "\n        Queens: ";
			printScore(std::cout, m_whiteData.queens);
			std::cout << " - ";
			printScore(std::cout, m_blackData.queens);

			std::cout << "\n         Kings: ";
			printScore(std::cout, m_whiteData.kings);
			std::cout << " - ";
			printScore(std::cout, m_blackData.kings);

			std::cout << "\n      Mobility: ";
			printScore(std::cout, m_whiteData.mobility);
			std::cout << " - ";
			printScore(std::cout, m_blackData.mobility);

			std::cout << "\n       Hanging: ";
			printScore(std::cout, m_whiteData.hanging);
			std::cout << " - ";
			printScore(std::cout, m_blackData.hanging);

			std::cout << "\n        Pinned: ";
			printScore(std::cout, m_whiteData.pinned);
			std::cout << " - ";
			printScore(std::cout, m_blackData.pinned);

			std::cout << "\n   King safety: ";
			printScore(std::cout, m_whiteData.kingSafety);
			std::cout << " - ";
			printScore(std::cout, m_blackData.kingSafety);

			std::cout << "\n      Total: ";
			printScore(std::cout, m_total);

			std::cout << "\n\nEval: ";
			printScore(std::cout, m_final);
			std::cout << "\n    with tempo bonus: ";
			printScore(std::cout, m_final + (m_pos.toMove() == Color::Black ? -Tempo : Tempo));

			std::cout << std::endl;
		}

		template <Color Us>
		void Evaluator::evalPawnStructure(const Position &pos, EvalData &ours, const EvalData &theirs)
		{
			constexpr auto Them = oppColor(Us);

			const auto &boards = pos.boards();

			const auto ourPawns = boards.pawns<Us>();
			const auto theirPawns = boards.pawns<Them>();

			const auto up = ourPawns.template shiftUpRelative<Us>();

			const auto doubledPawns = up & ourPawns;
			ours.pawnStructure += DoubledPawn * doubledPawns.popcount();

			ours.pawnStructure += DoubledGappedPawn * (up.template shiftUpRelative<Us>() & ourPawns).popcount();
			ours.pawnStructure += PawnDefender * (ours.pawnAttacks & ourPawns).popcount();
			ours.pawnStructure += OpenPawn * (ourPawns
				& ~theirPawns.template fillDownRelative<Us>() & ~ours.pawnAttacks).popcount();

			auto phalanx = ourPawns & ourPawns.shiftLeft();
			while (!phalanx.empty())
			{
				const auto square = phalanx.popLowestSquare();
				const auto rank = relativeRank<Us>(squareRank(square));

				ours.pawnStructure += PawnPhalanx[rank];
			}

			auto pawns = ourPawns;
			while (!pawns.empty())
			{
				const auto square = pawns.popLowestSquare();
				const auto pawn = Bitboard::fromSquare(square);

				const auto rank = relativeRank<Us>(squareRank(square));

				const auto antiPassers = theirPawns & AntiPasserMasks<Us>[static_cast<i32>(square)];

				if (antiPassers.empty())
				{
					ours.pawnStructure += Passer[rank];

					if (!(pawn & ours.pawnAttacks).empty())
						ours.pawnStructure += DefendedPasser[rank];
					if (!(pawn & doubledPawns).empty())
						ours.pawnStructure += DoubledPasser;

					const auto helpers = ourPawns & PawnHelperMasks<Us>[static_cast<i32>(square)];
					ours.pawnStructure += PasserHelper * helpers.popcount();

					ours.passers |= pawn;
				}
				else if (!(pawn & theirs.semiOpen).empty())
				{
					const auto stop = pawn.template shiftUpRelative<Us>();

					const auto levers = antiPassers
						& (pawn.template shiftUpLeftRelative<Us>() | pawn.template shiftUpRightRelative<Us>());

					if (antiPassers == levers)
						ours.pawnStructure += CandidatePasser[rank];
					else
					{
						const auto telelevers = antiPassers
							& (stop.template shiftUpLeftRelative<Us>() | stop.template shiftUpRightRelative<Us>());
						const auto helpers = ourPawns & (pawn.shiftLeft() | pawn.shiftRight());

						if (antiPassers == telelevers || telelevers.popcount() <= helpers.popcount())
							ours.pawnStructure += CandidatePasser[rank];
					}
				}
			}
		}

		template <Color Us>
		void Evaluator::evalPawns(const Position &pos, EvalData &ours, const EvalData &theirs)
		{
			constexpr auto Them = oppColor(Us);

			const auto &boards = pos.boards();

			ours.pawns += PawnAttackingMinor * (ours.pawnAttacks & boards.template minors<Them>()).popcount();
			ours.pawns += PawnAttackingRook  * (ours.pawnAttacks & boards.template  rooks<Them>()).popcount();
			ours.pawns += PawnAttackingQueen * (ours.pawnAttacks & boards.template queens<Them>()).popcount();

			auto passers = ours.passers;
			while (!passers.empty())
			{
				const auto square = passers.popLowestSquare();
				const auto passer = Bitboard::fromSquare(square);

				const auto rank = relativeRank<Us>(squareRank(square));

				const auto promotion = toSquare(relativeRank<Us>(7), squareFile(square));

				if (boards.template nonPk<Them>().empty()
					&& (std::min(5, chebyshev(square, promotion)) + (Us == pos.toMove() ? 1 : 0))
						< chebyshev(pos.template king<Them>(), promotion))
					ours.pawns += PasserSquareRule;

				if (!(passer.template shiftUpRelative<Us>() & boards.occupancy()).empty())
					ours.pawns += BlockedPasser[rank];
			}
		}

		template <Color Us>
		void Evaluator::evalKnights(const Position &pos, EvalData &ours, const EvalData &theirs)
		{
			constexpr auto Them = oppColor(Us);

			const auto &boards = pos.boards();

			auto knights = boards.knights<Us>();

			if (knights.empty())
				return;

			ours.knights += MinorBehindPawn
				* (knights.template shiftUpRelative<Us>() & boards.template pawns<Us>()).popcount();

			while (!knights.empty())
			{
				const auto square = knights.popLowestSquare();
				const auto knight = Bitboard::fromSquare(square);

				if ((AntiPasserMasks<Us>[static_cast<i32>(square)]
					& ~boards::Files[squareFile(square)]
					& boards.template pawns<Them>()).empty() && !(knight & ours.pawnAttacks).empty())
					ours.knights += KnightOutpost;

				const auto attacks = attacks::getKnightAttacks(square);

				ours.knights += MinorAttackingRook  * (attacks & boards.template  rooks<Them>()).popcount();
				ours.knights += MinorAttackingQueen * (attacks & boards.template queens<Them>()).popcount();

				ours.mobility += KnightMobility[(attacks & ours.available).popcount()];
			}
		}

		template <Color Us>
		void Evaluator::evalBishops(const Position &pos, EvalData &ours, const EvalData &theirs)
		{
			constexpr auto Them = oppColor(Us);

			const auto &boards = pos.boards();

			auto bishops = boards.bishops<Us>();

			if (bishops.empty())
				return;

			ours.bishops += MinorBehindPawn
				* (bishops.template shiftUpRelative<Us>() & boards.template pawns<Us>()).popcount();

			if (!(bishops & boards::DarkSquares).empty()
				&& !(bishops & boards::LightSquares).empty())
				ours.bishops += BishopPair;

			const auto occupancy = boards.occupancy();
			const auto xrayOcc = occupancy ^ boards.template bishops<Us>() ^ boards.template queens<Us>();

			while (!bishops.empty())
			{
				const auto square = bishops.popLowestSquare();

				const auto attacks = attacks::getBishopAttacks(square, occupancy);

				ours.bishops += MinorAttackingRook  * (attacks & boards.template  rooks<Them>()).popcount();
				ours.bishops += MinorAttackingQueen * (attacks & boards.template queens<Them>()).popcount();

				const auto mobilityAttacks = attacks::getBishopAttacks(square, xrayOcc);
				ours.mobility += BishopMobility[(mobilityAttacks & ours.available).popcount()];
			}
		}

		template <Color Us>
		void Evaluator::evalRooks(const Position &pos, EvalData &ours, const EvalData &theirs)
		{
			constexpr auto Them = oppColor(Us);

			const auto &boards = pos.boards();

			auto rooks = boards.rooks<Us>();

			if (rooks.empty())
				return;

			const auto occupancy = boards.occupancy();
			const auto xrayOcc = occupancy ^ boards.template rooks<Us>() ^ boards.template queens<Us>();

			while (!rooks.empty())
			{
				const auto square = rooks.lowestSquare();
				const auto rook = rooks.popLowestBit();

				if (!(rook & m_openFiles).empty())
					ours.rooks += RookOnOpenFile;
				else if (!(rook & ours.semiOpen).empty())
					ours.rooks += RookOnSemiOpenFile;

				if (!(rook.template fillUpRelative<Us>() & ours.passers).empty())
					ours.rooks += RookSupportingPasser;

				const auto attacks = attacks::getRookAttacks(square, occupancy);

				ours.rooks += RookAttackingQueen * (attacks & boards.template queens<Them>()).popcount();

				const auto mobilityAttacks = attacks::getRookAttacks(square, xrayOcc);
				ours.mobility += RookMobility[(mobilityAttacks & ours.available).popcount()];
			}
		}

		template <Color Us>
		void Evaluator::evalQueens(const Position &pos, EvalData &ours, const EvalData &theirs)
		{
			const auto &boards = pos.boards();

			auto queens = boards.queens<Us>();

			if (queens.empty())
				return;

			const auto occupancy = boards.occupancy();
			const auto xrayOcc = occupancy ^ boards.template bishops<Us>()
			    ^ boards.template rooks<Us>() ^ boards.template queens<Us>();

			while (!queens.empty())
			{
				const auto square = queens.popLowestSquare();

				const auto mobilityAttacks = attacks::getQueenAttacks(square, xrayOcc);
				ours.mobility += QueenMobility[(mobilityAttacks & ours.available).popcount()];
			}
		}

		template <Color Us>
		void Evaluator::evalKing(const Position &pos, EvalData &ours, const EvalData &theirs)
		{
			const auto &boards = pos.boards();

			const auto king = boards.template kings<Us>();

			if (!(king & m_openFiles).empty())
				ours.kings += KingOnOpenFile;
			else if (!(king & ours.semiOpen).empty())
				ours.kings += KingOnSemiOpenFile;
		}

		template <Color Us>
		void Evaluator::evalHangingAndPinned(const Position &pos, EvalData &ours, const EvalData &theirs)
		{
			//TODO
		}

		template <Color Us>
		void Evaluator::evalKingSafety(const Position &pos, EvalData &ours, const EvalData &theirs)
		{
			//TODO
		}
	}

	Score staticEval(const Position &pos, PawnCache *pawnCache)
	{
		return Evaluator{pos, pawnCache}.eval() * (pos.toMove() == Color::Black ? -1 : 1) + Tempo;
	}

	void printEval(const Position &pos, PawnCache *pawnCache)
	{
		Evaluator{pos, pawnCache}.printEval();
	}
}
