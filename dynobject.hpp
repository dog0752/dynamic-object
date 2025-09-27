/**
 * COPYRIGHT 2025 dog0752
 * this file is licensed under the dog0752-license-⑨.⑨
 *
 * this is a single file header only library that implements a
 * dynamic object system for C++
 */
#ifndef DYNOBJECT_HPP
#define DYNOBJECT_HPP

#include <any>
#include <expected>
#include <variant>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace dog0752
{
namespace dynobj
{

#ifdef DYNOBJECT_MULTITHREADED
#include <mutex>
#include <shared_mutex>
/* use real mutexes and lock guards */
using factory_mutex_t = std::mutex;
using object_mutex_t = std::shared_mutex;
template <typename T>
using unique_lock_t = std::unique_lock<T>;
template <typename T>
using shared_lock_t = std::shared_lock<T>;
#else
/* dummy, no-op mutexes and locks that do fuckall */
struct DummyMutex
{
}; /* an empty struct */
using factory_mutex_t = DummyMutex;
using object_mutex_t = DummyMutex;

/**
 * a lock guard that has an empty constructor/destructor
 * the compiler will optimize it away completely hopefully
 */
template <typename T>
struct DummyLock
{
	explicit DummyLock(T &)
	{
	}
};
template <typename T>
using unique_lock_t = DummyLock<T>;
template <typename T>
using shared_lock_t = DummyLock<T>;
#endif

class ObjectFactory
{
private:
	class Shape : public std::enable_shared_from_this<Shape>
	{
	public:
		/* constructor for the root shape */
		Shape()
			: parent_(nullptr), property_key_(0),
			  offset_(static_cast<size_t>(-1))
		{
		}

		/* constructor for a transition or a child shape */
		Shape(std::shared_ptr<const Shape> parent, size_t key)
			: parent_(std::move(parent)), property_key_(key),
			  offset_(parent_->getPropertyCount())
		{
		}

		/* looks up the memory offset for a given property identifier */
		std::expected<size_t, std::monostate> getOffset(size_t key) const
		{
			const Shape *current = this;
			/* the root's parent is null, so this loop always terminates */
			while (current->parent_)
			{
				if (current->property_key_ == key)
				{
					return current->offset_;
				}
				current = current->parent_.get();
			}
			return std::unexpected(std::monostate{}); /* signal "not found" */
		}

		inline size_t getNewOffset() const
		{
			return offset_;
		}
		inline size_t getPropertyCount() const
		{
			return offset_ == static_cast<size_t>(-1) ? 0 : offset_ + 1;
		}

	private:
		friend class ObjectFactory;

		/**
		 * parent_ points to the previous shape in the chain.
		 * A lookup walks up this chain until it finds the property or
		 * hits the root.
		 */
		std::shared_ptr<const Shape> parent_;

		/**
		 * property_key_ is the identifier for the new property that
		 * this shape
		 * adds relative to its parent.
		 */
		size_t property_key_;

		/**
		 * offset_ is the index in the values_ vector where the new
		 *property is stored.
		 */
		size_t offset_;

		/**
		 * caches the transition to a new shape when a property is added
		 */
		std::unordered_map<size_t, std::weak_ptr<Shape>> transitions_;
	};

public:
	/**
	 * a unique identifier for a property name, created by interning a
	 * string
	 */
	using Identifier = size_t;

	class DynObject
	{
	public:
		using Args = std::vector<std::any>;
		using Method = std::function<std::any(DynObject &, Args)>;

		/**
		 * the object's prototype for inheritance. properties not found
		 * on this object will be looked up on its prototype
		 */
		std::shared_ptr<DynObject> prototype = nullptr;

		template <typename T>
		void set(ObjectFactory &factory, Identifier key, T &&value)
		{
			unique_lock_t<object_mutex_t> lock(mutex_);

			auto maybe_offset = shape_->getOffset(key);

			if (maybe_offset.has_value())
			{
				/**
				 * property already exists. get the offset and update the value
				 */
				const size_t offset = *maybe_offset;
				values_[offset] = std::forward<T>(value);
			}
			else
			{
				/* property doesn't exist: this is a shape transition. */
				unique_lock_t<factory_mutex_t> factory_lock(
					factory.factory_mutex_);
				auto new_shape = factory.transition(shape_, key);
				shape_ = new_shape;

				/**
				 * pre allocate the vector to the new required
				 * size. avoids multiple reallocations
				 */
				values_.resize(new_shape->getPropertyCount());
				values_[new_shape->getNewOffset()] = std::forward<T>(value);
			}
		}

		template <typename T>
		std::expected<T, std::string> get(Identifier key) const
		{
			shared_lock_t<object_mutex_t> lock(mutex_);

			auto maybe_offset = shape_->getOffset(key);

			if (maybe_offset.has_value())
			{
				const size_t offset = *maybe_offset;
				if constexpr (std::is_same_v<T, std::any>)
					return values_[offset];
				if (const T *val = std::any_cast<T>(&(values_[offset])))
					return *val;
				return std::unexpected("type mismatch for property");
			}

#ifdef DYNOBJECT_MULTITHREADED
			lock.unlock(); /* unlock before recursing to mitigate deadlocks */
#endif

			if (prototype)
			{
				return prototype->get<T>(key);
			}

			return std::unexpected("no such property");
		}

		template <typename R = std::any>
		std::expected<R, std::string> call(Identifier name, Args args = {})
		{
			auto maybe_method = this->get<Method>(name);

			if (!maybe_method.has_value())
			{
				return std::unexpected(maybe_method.error());
			}

			const Method &method_to_call = maybe_method.value();
			std::any result = method_to_call(*this, std::move(args));

			if constexpr (std::is_same_v<R, std::any>)
			{
				return result;
			}
			else
			{
				if (const R *val = std::any_cast<R>(&result))
				{
					return *val;
				}
				return std::unexpected("type mismatch for method return value");
			}
		}

	private:
		friend class ObjectFactory;

		/**
		 * constructor is private, only the factory can create an object
		 */
		explicit DynObject(std::shared_ptr<Shape> initial_shape)
			: shape_(std::move(initial_shape))
		{
		}

		std::shared_ptr<Shape> shape_;
		std::vector<std::any> values_;
		mutable object_mutex_t mutex_;
	};

	/* FACTORY METHODS */

	ObjectFactory() : root_shape_(std::make_shared<Shape>())
	{
	}

	/* creates a new, empty dynamic object */
	std::unique_ptr<DynObject> createObject()
	{
		/**
		 * use a private constructor via new. std::make_unique cannot
		 * access it
		 */
		return std::unique_ptr<DynObject>(new DynObject(root_shape_));
	}

	Identifier intern(std::string_view str)
	{
		unique_lock_t<factory_mutex_t> lock(intern_mutex_);
		/**
		 * use transparent hashing to look up string_view without
		 * creating a string
		 */
		if (auto it = str_to_id_.find(str); it != str_to_id_.end())
		{
			return it->second;
		}

		Identifier id = id_to_str_.size();
		id_to_str_.emplace_back(str);
		/**
		 * emplace the string_view from our stable storage (id_to_str_)
		 * into the map
		 */
		str_to_id_.emplace(id_to_str_.back(), id);
		return id;
	}

	/**
	 * retrieves the original string from an interned identifier (for debugging)
	 */
	std::string_view getString(Identifier id) const
	{
		unique_lock_t<factory_mutex_t> lock(intern_mutex_);
		if (id < id_to_str_.size())
		{
			return id_to_str_[id];
		}
		return "<?>";
	}

private:
	/**
	 * calculates the new shape an object transitions to when a new property
	 * is added
	 */
	std::shared_ptr<Shape> transition(std::shared_ptr<Shape> from,
									  Identifier key)
	{
		/* check cache first */
		if (auto it = from->transitions_.find(key);
			it != from->transitions_.end())
		{
			if (auto next_shape = it->second.lock())
			{
				return next_shape;
			}
		}

		auto new_shape = std::make_shared<Shape>(from, key);

		/**
		 * cache the new transition using a weak_ptr to prevent cycles
		 */
		from->transitions_[key] = new_shape;
		return new_shape;
	}

	/* a transparent hasher for unordered_map lookups with string_view */
	struct StringHash
	{
		using is_transparent = void;
		size_t operator()(std::string_view sv) const

		{
			return std::hash<std::string_view>{}(sv);
		}
		size_t operator()(const std::string &s) const

		{
			return std::hash<std::string>{}(s);
		}
	};

	/* factory State */
	std::shared_ptr<Shape> root_shape_;
	factory_mutex_t factory_mutex_; /* for thread safe shape transitions */

	/* string interning state */
	mutable factory_mutex_t intern_mutex_;
	std::vector<std::string> id_to_str_;
	std::unordered_map<std::string, Identifier, StringHash, std::equal_to<>>
		str_to_id_;
};
} /* namespace dynobj */
} /* namespace dog0752 */

#endif /* #ifndef DYNOBJECT_HPP */
