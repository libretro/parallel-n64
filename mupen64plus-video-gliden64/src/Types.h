#ifndef TYPES_H
#define TYPES_H

typedef unsigned char			u8;	/* unsigned  8-bit */
typedef unsigned short			u16;	/* unsigned 16-bit */
typedef unsigned int			u32;	/* unsigned 32-bit */
#if defined(_MSC_VER)
typedef unsigned __int64		u64;	/* unsigned 64-bit */
typedef signed __int64			s64;	/* signed 64-bit */
#else
__extension__ typedef unsigned long long	u64;	/* unsigned 64-bit */
__extension__ typedef long long			s64;	/* signed 64-bit */
#endif

typedef signed char			s8;	/* signed  8-bit */
typedef short				s16;	/* signed 16-bit */
typedef int				s32;	/* signed 32-bit */

typedef volatile unsigned char		vu8;	/* unsigned  8-bit */
typedef volatile unsigned short		vu16;	/* unsigned 16-bit */
typedef volatile unsigned int		vu32;	/* unsigned 32-bit */
typedef volatile u64		vu64;	/* unsigned 64-bit */

typedef volatile signed char	vs8;	/* signed  8-bit */
typedef volatile short			vs16;	/* signed 16-bit */
typedef volatile int			vs32;	/* signed 32-bit */
typedef volatile s64		vs64;	/* signed 64-bit */

typedef float				f32;	/* single prec floating point */
typedef double				f64;	/* double prec floating point */

/* unnamed members: MSVC takes them in C, GCC with __extension__ */
#if defined(__GNUC__) && !defined(__cplusplus)
#define GLI_ANON __extension__
#else
#define GLI_ANON
#endif

#ifndef TRUE
#define TRUE    1
#endif

#ifndef FALSE
#define FALSE   0
#endif

#ifndef NULL
#define NULL    0
#endif

#ifndef PLUGIN_PATH_SIZE
#define PLUGIN_PATH_SIZE 1024 /* bytes of a UTF-8 path */
#endif

#ifdef __cplusplus
template <typename T>
class ValueKeeper
{
public:
	ValueKeeper(T& _obj, T _newVal)
		: m_obj(_obj)
		, m_val(_obj)
	{
		m_obj = _newVal;
	}

	~ValueKeeper()
	{
		m_obj = m_val;
	}

private:
	T & m_obj;
	T m_val;
};
#endif

#endif /* TYPES_H */
