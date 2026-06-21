// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include "Misc.h"
#include "Aliases.h"
#include <cstring>
#include <optional>

namespace FusionCore
{
	class WriteBuffer;

	struct ResetPoint
	{
	private:
		size_t       _offset{0};
		WriteBuffer* _buffer{nullptr};

	public:
		explicit ResetPoint(WriteBuffer* buffer);

		void Use();
	};

	class ReadBuffer
	{
		size_t   _offset{0};
		size_t   _length{0};
		uint8_t* _buffer{nullptr};

		std::optional<double> _timeBase{};

		// set once a read is attempted past the end of the buffer; every
		// subsequent read becomes a no-op returning zero. enforced in ALL build
		// configs — the old assert-only guard was a no-op under NDEBUG, leaving
		// the untrusted receive path open to OOB reads in release.
		bool _failed{false};

		bool Use(size_t length, size_t& offset);

		template <typename T, std::enable_if_t<!std::is_pointer_v<T>, bool> = true>
		T Read()
		{
			T      value{};
			size_t offset;
			if (Use(sizeof(T), offset))
			{
				memcpy(&value, _buffer + offset, sizeof(T));
			}
			return value;
		}

	public:
		explicit ReadBuffer(Data data);

		[[nodiscard]] size_t Offset() const { return _offset; }

		[[nodiscard]] size_t Remaining() const { return _length - _offset; }

		uint8_t  Byte() { return Read<uint8_t>(); }
		int8_t   Sbyte() { return Read<int8_t>(); }
		uint16_t UShort() { return Read<uint16_t>(); }
		int16_t  Short() { return Read<int16_t>(); }
		uint32_t UInt() { return Read<uint32_t>(); }
		int32_t  Int() { return Read<int32_t>(); }
		uint64_t ULong() { return Read<uint64_t>(); }
		int64_t  Long() { return Read<int64_t>(); }

		double Double() { return Read<double>(); }
		float  Float() { return Read<float>(); }

		uint64_t ULongVar();

		FusionCore::Data ReadDataAll();

		int64_t LongVar() { return ZigZagDecode(static_cast<int64_t>(ULongVar())); }

		int16_t  ShortVar() { return static_cast<int16_t>(LongVar()); }
		uint16_t UShortVar() { return static_cast<uint16_t>(ULongVar()); }

		int32_t  IntVar() { return static_cast<int32_t>(LongVar()); }
		uint32_t UIntVar() { return static_cast<uint32_t>(ULongVar()); }

		void ReadData(FusionCore::Data& data);

		void Skip(const size_t length)
		{
			size_t offset;
			Use(length, offset);
		}

		// true if any read ran past the end of the buffer — a truncated or
		// malformed packet. receive-path parse loops should check this and abort.
		[[nodiscard]] bool Failed() const { return _failed; }

		uint8_t Flags() { return Byte(); }

		bool Bool() { return Byte() == 1; }

		PlayerId        Player() { return UShortVar(); }
		FusionCore::Map ReadMap() { return UShortVar(); }

		void Versions(int32_t& plugin_version, int32_t& client_version);

		FusionCore::ObjectId ReadObjectId()
		{
			FusionCore::ObjectId id{};
			id.Origin  = Player();
			id.Map     = ReadMap();
			id.Counter = UIntVar();
			return id;
		}

		double TimeBase();

		double Time();
	};

	struct WriteFlags
	{
		WriteBuffer* buffer;
		size_t       offset;

		void AddFlags(uint8_t flag) const;
	};

	class WriteBuffer
	{
		friend struct WriteFlags;
		size_t                _offset{0};
		Data                  _buffer{};
		std::optional<double> _timeBase{};

		size_t Use(size_t length);

		template <typename T, std::enable_if_t<!std::is_pointer_v<T>, bool> = true>
		void Write(T value)
		{
			auto offset = Use(sizeof(T));
			memcpy(_buffer.Ptr + offset, &value, sizeof(T));
		}

	public:
		ResetPoint GetResetPoint() { return ResetPoint(this); }

		Data Take()
		{
			auto d = _buffer;

			// >=, not >: Use() only ever advances _offset up to the buffer's
			// capacity (it Resize()s first), so a writer that ends exactly
			// full (_offset == Length) is a valid, benign state — e.g. a
			// small dedicated buffer like the interest-key payload, which
			// Use() grows to exactly its content size on first write. The
			// old `>` aborted on that case in checked builds.
			assert(d.Length >= _offset);

			d.Length = _offset;

			_offset = 0;
			_buffer = FusionCore::Data{};

			return d;
		}

		bool Empty() const { return _offset == 0; }

		[[nodiscard]] size_t Offset() const { return _offset; }

		void WriteObjectId(const FusionCore::ObjectId id)
		{
			Player(id.Origin);
			WriteMap(id.Map);
			UIntVar(id.Counter);
		}

		void Player(const PlayerId id) { UShortVar(id); }
		void WriteMap(const FusionCore::Map map) { UShortVar(map); }

		WriteFlags Flags()
		{
			const auto offset   = Use(1);
			_buffer.Ptr[offset] = 0;
			return WriteFlags{this, offset};
		}

		void Byte(const uint8_t value) { Write(value); }
		void Sbyte(const int8_t value) { Write(value); }
		void UShort(const uint16_t value) { Write(value); }
		void Short(const int16_t value) { Write(value); }
		void UInt(const uint32_t value) { Write(value); }
		void Int(const int32_t value) { Write(value); }
		void ULong(const uint64_t value) { Write(value); }
		void Long(const int64_t value) { Write(value); }

		void ULongVar(uint64_t value);

		void LongVar(const int64_t value) { ULongVar(static_cast<uint64_t>(ZigZagEncode(value))); }

		void UIntVar(const uint32_t value) { ULongVar(value); }
		void IntVar(const int32_t value) { LongVar(value); }

		void UShortVar(const uint16_t value) { ULongVar(value); }
		void ShortVar(const int16_t value) { LongVar(value); }

		void Float(const float value) { Write(value); }
		void Double(const double value) { Write(value); }

		void Span(const BufferT<uint8_t>& data);

		void Time(double time);

		void TimeBase(double time);

		bool Bool(bool value);

		void Versions(int32_t plugin_version, int32_t client_version, int32_t client_base_version);

		void Clear() const;

		void WriteDataAll(const FusionCore::Data& data);

		void WriteData(const FusionCore::Data& data);

		friend struct ResetPoint;
	};
} // namespace FusionCore

#include "SharedModeCompat.h"
