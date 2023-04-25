import numpy as np

def generate(u=1, seq_length=813, q=0, dtype=np.complex64):
    """
    Generate a Zadoff-Chu (ZC) sequence.
    Parameters
    ----------
    u : int
        Root index of the the ZC sequence: u>0.
    seq_length : int
        Length of the sequence to be generated. Usually a prime number:
        u<seq_length, greatest-common-denominator(u,seq_length)=1.
    q : int
        Cyclic shift of the sequence (default 0).
    Returns
    -------
    zcseq : 1D ndarray of complex floats
        ZC sequence generated.
        
   To still put DC to 0, 
   we interleave the ZC sequence with zeros.
 
    """

    for el in [u, seq_length, q]:
        if not float(el).is_integer():
            raise ValueError('{} is not an integer'.format(el))
    if u <= 0:
        raise ValueError('u is not stricly positive')
    if u >= seq_length:
        raise ValueError('u is not stricly smaller than seq_length')

    if np.gcd(u, seq_length) != 1:
        raise ValueError('the greatest common denominator of u and seq_length is not 1')

    cf = seq_length % 2
    n = np.arange(seq_length)
    zcseq = np.exp(-1j * np.pi * u * n * (n+cf+2.0*q) / seq_length, dtype=dtype)

    return zcseq

NZC = 813  
num_samples = 1024

samples = np.zeros(num_samples, dtype=np.complex64)
yzch = generate(u=1, seq_length=NZC)


# padd with zeros 
samples[:NZC] = yzch

# Conform to:
# a[0] should contain the zero frequency term,
# a[1:n//2] should contain the positive-frequency terms,
# a[n//2 + 1:] should contain the negative-frequency terms, in increasing order starting from the most negative frequency.
#samples = np.fft.ifftshift(samples)
samples = np.roll(samples, -NZC//2)

time_samples = np.fft.ifft(samples)


# Scaling to not go aboce power 1 when upsampling in the DAC
factor = 0.85 * 1/max(np.max(abs(time_samples.real)), np.max(abs(time_samples.imag)))
time_samples = factor*time_samples

dt = np.dtype([('re', np.int16), ('im', np.int16)])

time_samples_int16 = np.zeros(num_samples, dtype=dt)
time_samples_int16['re'] = time_samples.real * (2**15)
time_samples_int16['im'] = time_samples.imag * (2**15)

time_samples_int16.tofile('zc-sequence-sc16.dat')